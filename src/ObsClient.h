#pragma once

#include "AppTypes.h"
#include "Diagnostics.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

// Lightweight obs-websocket v5 client. Controls OBS Studio recording over the
// built-in WebSocket server (default ws://127.0.0.1:4455). Does not capture or
// encode anything itself; OBS records whatever source the user configured.
class ObsClient {
public:
    ObsClient() = default;
    ~ObsClient();

    ObsClient(const ObsClient&) = delete;
    ObsClient& operator=(const ObsClient&) = delete;

    bool Start(
        const ObsSettings& settings,
        bool sessionEnabled,
        Diagnostics* diagnostics);
    void Stop();
    void ApplySettings(const ObsSettings& settings, bool sessionEnabled);

    bool StartRecord();
    bool StopRecord();

    ObsConnState ConnectionState() const;
    bool Recording() const;
    std::chrono::seconds Elapsed() const;
    std::wstring LastError() const;

    // One-shot connect + identify. The UI runs it on a separate thread; it does
    // not activate or otherwise change the main OBS session.
    static bool TestConnection(
        const ObsSettings& settings,
        std::wstring& error,
        const std::atomic<bool>* cancel = nullptr);

private:
    enum class Command {
        StartRecord,
        StopRecord
    };

    void Run(ObsSettings settings);
    bool QueueCommand(Command command);
    void SetState(ObsConnState state);
    void SetRecording(bool recording);
    void SetError(std::wstring message);
    void DiscardCommands();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> reconnect_{false};
    std::atomic<bool> sessionActive_{false};
    std::atomic<int> connState_{static_cast<int>(ObsConnState::Disconnected)};
    std::atomic<bool> recording_{false};
    std::deque<Command> commands_;
    ObsSettings settings_;
    std::wstring lastError_;
    std::chrono::steady_clock::time_point recordStartedAt_{};
    Diagnostics* diagnostics_ = nullptr;
};
