#include "ObsClient.h"

#include "ObsProtocol.h"
#include "TextUtil.h"
#include "WinHttpWebSocket.h"

#include <algorithm>
#include <deque>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kIdentifyTimeout = std::chrono::seconds(5);
constexpr auto kReconnectDelay = std::chrono::seconds(2);

bool IsCancelled(
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel = nullptr)
{
    return (cancel && cancel->load()) ||
        (secondaryCancel && secondaryCancel->load());
}

bool ReceiveUntil(
    WinHttpWebSocket& transport,
    std::string& message,
    Clock::time_point deadline,
    std::wstring& error,
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel)
{
    while (Clock::now() < deadline && !IsCancelled(cancel, secondaryCancel)) {
        const auto result = transport.ReceiveText(
            message,
            error,
            cancel,
            secondaryCancel);
        if (result == WinHttpWebSocket::ReceiveResult::Message) {
            return true;
        }
        if (result == WinHttpWebSocket::ReceiveResult::Idle) {
            continue;
        }
        return false;
    }
    return false;
}

bool IdentifyObs(
    WinHttpWebSocket& transport,
    const ObsSettings& settings,
    std::wstring& error,
    const std::atomic<bool>* cancel,
    const std::atomic<bool>* secondaryCancel = nullptr)
{
    std::string message;
    if (!ReceiveUntil(
            transport,
            message,
            Clock::now() + kIdentifyTimeout,
            error,
            cancel,
            secondaryCancel)) {
        if (!IsCancelled(cancel, secondaryCancel) && error.empty()) {
            error = L"OBS did not send Hello";
        }
        return false;
    }

    ObsHello hello;
    if (!ParseObsHello(message, hello)) {
        error = L"OBS sent an invalid Hello message";
        return false;
    }
    if (hello.requiresAuthentication && settings.password.empty()) {
        error = L"OBS requires a WebSocket password";
        return false;
    }

    try {
        if (!transport.SendText(
                BuildObsIdentify(WideToUtf8(settings.password), hello),
                error)) {
            return false;
        }
    } catch (const std::exception&) {
        error = L"Could not create OBS authentication response";
        return false;
    }

    message.clear();
    if (!ReceiveUntil(
            transport,
            message,
            Clock::now() + kIdentifyTimeout,
            error,
            cancel,
            secondaryCancel)) {
        if (!IsCancelled(cancel, secondaryCancel) && error.empty()) {
            error = L"OBS Identify timed out";
        }
        return false;
    }
    if (!IsObsIdentified(message)) {
        error = hello.requiresAuthentication
            ? L"OBS authentication failed (wrong password?)"
            : L"OBS rejected Identify";
        return false;
    }
    return true;
}

} // namespace

ObsClient::~ObsClient()
{
    Stop();
}

bool ObsClient::Start(
    const ObsSettings& settings,
    bool sessionEnabled,
    Diagnostics* diagnostics)
{
    std::scoped_lock lock(mutex_);
    if (worker_.joinable()) {
        return true;
    }
    diagnostics_ = diagnostics;
    settings_ = settings;
    stop_.store(false);
    reconnect_.store(false);
    sessionActive_.store(sessionEnabled);
    commands_.clear();
    worker_ = std::thread(&ObsClient::Run, this, settings);
    return true;
}

void ObsClient::Stop()
{
    if (!worker_.joinable()) {
        return;
    }
    stop_.store(true);
    cv_.notify_all();
    worker_.join();
    sessionActive_.store(false);
    SetState(ObsConnState::Disconnected);
    SetRecording(false);
    DiscardCommands();
}

void ObsClient::ApplySettings(
    const ObsSettings& settings,
    bool sessionEnabled)
{
    {
        std::scoped_lock lock(mutex_);
        settings_ = settings;
        commands_.clear();
        lastError_.clear();
    }
    sessionActive_.store(sessionEnabled);
    reconnect_.store(true);
    cv_.notify_all();
}

bool ObsClient::StartRecord()
{
    return QueueCommand(Command::StartRecord);
}

bool ObsClient::StopRecord()
{
    return QueueCommand(Command::StopRecord);
}

bool ObsClient::QueueCommand(Command command)
{
    if (ConnectionState() != ObsConnState::Connected) {
        SetError(L"OBS not connected");
        return false;
    }

    {
        std::scoped_lock lock(mutex_);
        commands_.push_back(command);
    }
    if (diagnostics_) {
        diagnostics_->LogDebug(
            command == Command::StartRecord
                ? L"OBS: start command queued"
                : L"OBS: stop command queued");
    }
    cv_.notify_all();
    return true;
}

ObsConnState ObsClient::ConnectionState() const
{
    return static_cast<ObsConnState>(connState_.load());
}

bool ObsClient::Recording() const
{
    return recording_.load();
}

std::chrono::seconds ObsClient::Elapsed() const
{
    if (!recording_.load()) {
        return std::chrono::seconds{0};
    }
    std::scoped_lock lock(mutex_);
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now() - recordStartedAt_);
    return std::max(elapsed, std::chrono::seconds{0});
}

std::wstring ObsClient::LastError() const
{
    std::scoped_lock lock(mutex_);
    return lastError_;
}

void ObsClient::SetState(ObsConnState state)
{
    connState_.store(static_cast<int>(state));
}

void ObsClient::SetRecording(bool recording)
{
    const bool wasRecording = recording_.exchange(recording);
    if (recording && !wasRecording) {
        std::scoped_lock lock(mutex_);
        recordStartedAt_ = Clock::now();
    }
}

void ObsClient::SetError(std::wstring message)
{
    bool changed = false;
    {
        std::scoped_lock lock(mutex_);
        changed = message != lastError_;
        lastError_ = message;
    }
    if (diagnostics_ && changed && !message.empty()) {
        diagnostics_->LogWarning(L"OBS: " + message);
    }
}

void ObsClient::DiscardCommands()
{
    std::scoped_lock lock(mutex_);
    commands_.clear();
}

void ObsClient::Run(ObsSettings settings)
{
    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return stop_.load() || sessionActive_.load();
            });
            if (stop_.load()) {
                break;
            }
            settings = settings_;
        }
        reconnect_.store(false);

        if (!sessionActive_.load()) {
            SetState(ObsConnState::Disconnected);
            continue;
        }

        SetState(ObsConnState::Connecting);
        WinHttpWebSocket transport;
        std::wstring error;
        if (!transport.Connect(settings, error, &stop_, &reconnect_) ||
            !IdentifyObs(transport, settings, error, &stop_, &reconnect_)) {
            SetState(ObsConnState::Disconnected);
            SetRecording(false);
            DiscardCommands();
            if (!stop_.load() && !reconnect_.load() && !error.empty()) {
                SetError(error);
            }
            if (stop_.load()) {
                break;
            }
            if (reconnect_.load() || !sessionActive_.load()) {
                continue;
            }
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, kReconnectDelay, [this] {
                return stop_.load() ||
                    reconnect_.load() ||
                    !sessionActive_.load();
            });
            continue;
        }

        SetState(ObsConnState::Connected);
        SetError({});
        if (diagnostics_) {
            diagnostics_->LogInfo(
                L"OBS: connected to " + settings.host + L":" +
                std::to_wstring(settings.port));
        }

        struct ReceiveInbox {
            std::mutex mutex;
            std::deque<std::string> messages;
            bool ended = false;
            WinHttpWebSocket::ReceiveResult result =
                WinHttpWebSocket::ReceiveResult::Idle;
            std::wstring error;
        } inbox;

        std::thread receiver([this, &transport, &inbox] {
            for (;;) {
                std::string message;
                std::wstring receiveError;
                const auto result = transport.ReceiveText(
                    message,
                    receiveError,
                    &stop_,
                    &reconnect_);
                if (result == WinHttpWebSocket::ReceiveResult::Idle) {
                    continue;
                }
                if (result == WinHttpWebSocket::ReceiveResult::Message) {
                    {
                        std::scoped_lock lock(inbox.mutex);
                        inbox.messages.push_back(std::move(message));
                    }
                    cv_.notify_all();
                    continue;
                }

                {
                    std::scoped_lock lock(inbox.mutex);
                    inbox.ended = true;
                    inbox.result = result;
                    inbox.error = std::move(receiveError);
                }
                cv_.notify_all();
                return;
            }
        });

        std::uint64_t requestId = 1;
        bool connected = transport.SendText(
            BuildObsRequest("GetRecordStatus", requestId++),
            error);

        while (connected &&
               !stop_.load() &&
               !reconnect_.load() &&
               sessionActive_.load()) {
            std::deque<Command> commands;
            {
                std::scoped_lock lock(mutex_);
                commands.swap(commands_);
            }
            for (const Command command : commands) {
                const char* requestType =
                    command == Command::StartRecord ? "StartRecord" :
                    "StopRecord";
                if (!transport.SendText(
                        BuildObsRequest(requestType, requestId++),
                        error)) {
                    connected = false;
                    break;
                }
                if (diagnostics_) {
                    diagnostics_->LogInfo(
                        command == Command::StartRecord
                            ? L"OBS: start command sent"
                            : L"OBS: stop command sent");
                }
                if (!transport.SendText(
                        BuildObsRequest("GetRecordStatus", requestId++),
                        error)) {
                    connected = false;
                    break;
                }
            }
            if (!connected) {
                break;
            }

            std::deque<std::string> messages;
            bool receiveEnded = false;
            WinHttpWebSocket::ReceiveResult receiveResult =
                WinHttpWebSocket::ReceiveResult::Idle;
            std::wstring receiveError;
            {
                std::scoped_lock lock(inbox.mutex);
                messages.swap(inbox.messages);
                receiveEnded = inbox.ended;
                receiveResult = inbox.result;
                receiveError = inbox.error;
            }

            for (const std::string& message : messages) {
                ObsRecordingUpdate update;
                if (ParseObsRecordingUpdate(message, update)) {
                    SetRecording(update.active);
                    if (update.active && update.hasDuration) {
                        std::scoped_lock lock(mutex_);
                        recordStartedAt_ = Clock::now() - update.duration;
                    }
                    continue;
                }

                ObsRequestResult requestResult;
                if (ParseObsRequestResult(message, requestResult) &&
                    (requestResult.requestType == "StartRecord" ||
                     requestResult.requestType == "StopRecord")) {
                    if (requestResult.success) {
                        const bool start =
                            requestResult.requestType == "StartRecord";
                        SetRecording(start);
                        if (diagnostics_) {
                            diagnostics_->LogInfo(
                                start
                                    ? L"OBS: start command accepted"
                                    : L"OBS: stop command accepted");
                        }
                    } else if (diagnostics_) {
                        std::wstring rejection =
                            L"OBS: " +
                            Utf8ToWide(requestResult.requestType) +
                            L" rejected (" +
                            std::to_wstring(requestResult.code) + L")";
                        if (!requestResult.comment.empty()) {
                            rejection += L": " +
                                Utf8ToWide(requestResult.comment);
                        }
                        diagnostics_->LogWarning(rejection);
                    }
                    continue;
                }

                int op = -1;
                if (!ParseObsOp(message, op)) {
                    if (diagnostics_) {
                        diagnostics_->LogWarning(
                            L"OBS: ignored malformed JSON message");
                    }
                } else if (op != 5 && op != 7 && diagnostics_) {
                    diagnostics_->LogDebug(
                        L"OBS: ignored unsupported message opcode " +
                        std::to_wstring(op));
                }
            }

            if (receiveEnded) {
                if (receiveResult == WinHttpWebSocket::ReceiveResult::Error &&
                    !receiveError.empty()) {
                    error = std::move(receiveError);
                }
                break;
            }

            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100));
        }

        transport.BeginClose();
        if (receiver.joinable()) {
            receiver.join();
        }
        transport.Close();
        SetState(ObsConnState::Disconnected);
        SetRecording(false);
        DiscardCommands();
        if (!stop_.load() &&
            !reconnect_.load() &&
            sessionActive_.load() &&
            !error.empty()) {
            SetError(error);
        }
        if (diagnostics_ && !stop_.load()) {
            diagnostics_->LogInfo(L"OBS: disconnected");
        }

        if (!stop_.load() &&
            !reconnect_.load() &&
            sessionActive_.load()) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, kReconnectDelay, [this] {
                return stop_.load() ||
                    reconnect_.load() ||
                    !sessionActive_.load();
            });
        }
    }

    SetState(ObsConnState::Disconnected);
}

bool ObsClient::TestConnection(
    const ObsSettings& settings,
    std::wstring& error,
    const std::atomic<bool>* cancel)
{
    error.clear();
    if (cancel && cancel->load()) {
        return false;
    }

    WinHttpWebSocket transport;
    return transport.Connect(settings, error, cancel) &&
        IdentifyObs(transport, settings, error, cancel);
}
