#pragma once

#include "AppHost.h"

#include <Windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

// Hosts the settings UI inside a WebView2 control. The window is frameless —
// the title bar, sidebar, and pages are all HTML (see web/). This class only
// owns the OS window + tray, bridges messages to AppHost, and pumps status.
class WebView2Ui {
public:
    explicit WebView2Ui(AppHost& host);
    ~WebView2Ui();

    WebView2Ui(const WebView2Ui&) = delete;
    WebView2Ui& operator=(const WebView2Ui&) = delete;

    bool Create(HINSTANCE instance, int showCommand);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void InitWebView();
    void OnWebViewReady();
    void ResizeWebView();
    void HandleWebMessage(const std::wstring& json);
    void StartTestConnection(const ObsSettings& settings);
    void FinishTestConnection(std::uint64_t generation);
    void CancelTestConnection();

    // host -> UI pushes
    void PushSettings();
    void PushAbout();
    void PushStatus();
    void PostJson(const std::wstring& json);

    // tray
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMinimizedNotification();
    bool CloseToTrayEnabled() const;
    void HandleWindowMinimize();
    void HandleWindowClose();
    void HideToTray();
    void ShowFromTray();
    void ShowTrayMenu();
    void ExitApplication();

    AppHost& host_;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    bool classRegistered_ = false;
    bool trayIconAdded_ = false;
    bool exiting_ = false;
    std::thread testThread_;
    std::atomic<bool> testCancel_{false};
    std::mutex testResultMutex_;
    std::uint64_t testGeneration_ = 0;
    bool testResultReady_ = false;
    bool testResultOk_ = false;
    std::wstring testResultError_;
    ObsSettings testResultSettings_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
