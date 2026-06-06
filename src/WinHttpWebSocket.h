#pragma once

#include "AppTypes.h"

#include <atomic>
#include <memory>
#include <string>

class WinHttpWebSocket {
public:
    enum class ReceiveResult {
        Message,
        Idle,
        Closed,
        Cancelled,
        Error
    };

    WinHttpWebSocket();
    ~WinHttpWebSocket();

    WinHttpWebSocket(const WinHttpWebSocket&) = delete;
    WinHttpWebSocket& operator=(const WinHttpWebSocket&) = delete;

    bool Connect(
        const ObsSettings& settings,
        std::wstring& error,
        const std::atomic<bool>* cancel = nullptr,
        const std::atomic<bool>* secondaryCancel = nullptr);
    bool SendText(const std::string& message, std::wstring& error);
    ReceiveResult ReceiveText(
        std::string& message,
        std::wstring& error,
        const std::atomic<bool>* cancel = nullptr,
        const std::atomic<bool>* secondaryCancel = nullptr);
    void BeginClose();
    void Close();

private:
    struct Handles;
    std::unique_ptr<Handles> handles_;
};
