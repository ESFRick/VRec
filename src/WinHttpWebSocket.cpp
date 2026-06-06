#include "WinHttpWebSocket.h"

#include "TextUtil.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace {

constexpr int kResolveTimeoutMs = 5000;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kSendTimeoutMs = 5000;
constexpr int kReceiveTimeoutMs = 200;
constexpr size_t kMaxMessageBytes = 8 * 1024 * 1024;

bool IsCancelled(
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel)
{
    return (cancel && cancel->load()) ||
        (secondaryCancel && secondaryCancel->load());
}

std::wstring WinHttpError(const wchar_t* action, DWORD error)
{
    return std::wstring(action) + L": " + GetLastErrorString(error);
}

class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value)
        : value_(value)
    {
    }

    ~InternetHandle()
    {
        reset();
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    InternetHandle(InternetHandle&& other) noexcept
        : value_(other.value_.exchange(nullptr))
    {
    }

    InternetHandle& operator=(InternetHandle&& other) noexcept
    {
        if (this != &other) {
            reset(other.value_.exchange(nullptr));
        }
        return *this;
    }

    HINTERNET get() const { return value_.load(); }
    explicit operator bool() const { return value_.load() != nullptr; }

    void reset(HINTERNET value = nullptr)
    {
        const HINTERNET previous = value_.exchange(value);
        if (previous) {
            WinHttpCloseHandle(previous);
        }
    }

private:
    std::atomic<HINTERNET> value_{nullptr};
};

} // namespace

struct WinHttpWebSocket::Handles {
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    InternetHandle socket;
};

WinHttpWebSocket::WinHttpWebSocket()
    : handles_(std::make_unique<Handles>())
{
}

WinHttpWebSocket::~WinHttpWebSocket()
{
    Close();
}

bool WinHttpWebSocket::Connect(
    const ObsSettings& settings,
    std::wstring& error,
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel)
{
    Close();
    error.clear();
    if (IsCancelled(cancel, secondaryCancel)) {
        return false;
    }

    handles_->session.reset(WinHttpOpen(
        L"VRec/1.0.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!handles_->session) {
        error = WinHttpError(L"WinHTTP session creation failed", GetLastError());
        return false;
    }
    if (!WinHttpSetTimeouts(
            handles_->session.get(),
            kResolveTimeoutMs,
            kConnectTimeoutMs,
            kSendTimeoutMs,
            kReceiveTimeoutMs)) {
        error = WinHttpError(L"WinHTTP timeout setup failed", GetLastError());
        return false;
    }

    const std::wstring host = settings.host.empty() ? L"127.0.0.1" : settings.host;
    handles_->connection.reset(WinHttpConnect(
        handles_->session.get(),
        host.c_str(),
        static_cast<INTERNET_PORT>(settings.port),
        0));
    if (!handles_->connection) {
        error = WinHttpError(L"Cannot prepare OBS connection", GetLastError());
        return false;
    }

    handles_->request.reset(WinHttpOpenRequest(
        handles_->connection.get(),
        L"GET",
        L"/",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0));
    if (!handles_->request) {
        error = WinHttpError(L"OBS WebSocket request creation failed", GetLastError());
        return false;
    }

    if (!WinHttpSetOption(
            handles_->request.get(),
            WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
            nullptr,
            0)) {
        error = WinHttpError(L"OBS WebSocket upgrade setup failed", GetLastError());
        return false;
    }
    if (IsCancelled(cancel, secondaryCancel)) {
        return false;
    }

    if (!WinHttpSendRequest(
            handles_->request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        const DWORD code = GetLastError();
        if (!IsCancelled(cancel, secondaryCancel)) {
            error = code == ERROR_WINHTTP_NAME_NOT_RESOLVED
                ? L"Cannot resolve OBS host " + host
                : L"Cannot connect to OBS at " + host + L":" + std::to_wstring(settings.port);
        }
        return false;
    }
    if (!WinHttpReceiveResponse(handles_->request.get(), nullptr)) {
        const DWORD code = GetLastError();
        if (!IsCancelled(cancel, secondaryCancel)) {
            error = code == ERROR_WINHTTP_NAME_NOT_RESOLVED
                ? L"Cannot resolve OBS host " + host
                : L"Cannot connect to OBS at " + host + L":" + std::to_wstring(settings.port);
        }
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            handles_->request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        error = WinHttpError(L"OBS WebSocket status query failed", GetLastError());
        return false;
    }
    if (statusCode != HTTP_STATUS_SWITCH_PROTOCOLS) {
        error = L"OBS WebSocket did not accept upgrade (HTTP " + std::to_wstring(statusCode) + L")";
        return false;
    }

    handles_->socket.reset(WinHttpWebSocketCompleteUpgrade(handles_->request.get(), 0));
    if (!handles_->socket) {
        error = WinHttpError(L"OBS WebSocket upgrade failed", GetLastError());
        return false;
    }
    handles_->request.reset();
    return !IsCancelled(cancel, secondaryCancel);
}

bool WinHttpWebSocket::SendText(const std::string& message, std::wstring& error)
{
    if (!handles_->socket) {
        error = L"OBS WebSocket is not connected";
        return false;
    }
    if (message.size() > std::numeric_limits<DWORD>::max()) {
        error = L"OBS WebSocket message is too large";
        return false;
    }

    const DWORD result = WinHttpWebSocketSend(
        handles_->socket.get(),
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(message.data()),
        static_cast<DWORD>(message.size()));
    if (result != NO_ERROR) {
        error = WinHttpError(L"OBS WebSocket send failed", result);
        return false;
    }
    return true;
}

WinHttpWebSocket::ReceiveResult WinHttpWebSocket::ReceiveText(
    std::string& message,
    std::wstring& error,
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel)
{
    message.clear();
    if (!handles_->socket) {
        error = L"OBS WebSocket is not connected";
        return ReceiveResult::Error;
    }

    std::array<char, 16 * 1024> buffer{};
    for (;;) {
        if (IsCancelled(cancel, secondaryCancel)) {
            return ReceiveResult::Cancelled;
        }

        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType{};
        const DWORD result = WinHttpWebSocketReceive(
            handles_->socket.get(),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytesRead,
            &bufferType);

        if (result == ERROR_WINHTTP_TIMEOUT && message.empty()) {
            return IsCancelled(cancel, secondaryCancel)
                ? ReceiveResult::Cancelled
                : ReceiveResult::Idle;
        }
        if (result == ERROR_WINHTTP_OPERATION_CANCELLED && IsCancelled(cancel, secondaryCancel)) {
            return ReceiveResult::Cancelled;
        }
        if (result != NO_ERROR) {
            error = WinHttpError(L"OBS WebSocket receive failed", result);
            return ReceiveResult::Error;
        }
        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            return ReceiveResult::Closed;
        }
        if (bufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
            bufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
            error = L"OBS sent an unexpected binary WebSocket message";
            return ReceiveResult::Error;
        }
        if (message.size() + bytesRead > kMaxMessageBytes) {
            error = L"OBS WebSocket message exceeded the size limit";
            return ReceiveResult::Error;
        }

        message.append(buffer.data(), bytesRead);
        if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            return ReceiveResult::Message;
        }
    }
}

void WinHttpWebSocket::Close()
{
    BeginClose();
    handles_->request.reset();
    handles_->connection.reset();
    handles_->session.reset();
}

void WinHttpWebSocket::BeginClose()
{
    handles_->socket.reset();
}
