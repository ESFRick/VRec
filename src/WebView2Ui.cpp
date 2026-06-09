#include "WebView2Ui.h"

#include "AppVersion.h"
#include "TextUtil.h"
#include "WebBridgeCodec.h"
#include "resource.h"

#include <WebView2.h>
#include <wrl/event.h>

#include <ShlObj.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const wchar_t* kWindowClass = L"VRecWebWindow";
#ifdef _DEBUG
const wchar_t* kWindowTitle = L"VRec DEBUG BUILD";
#else
const wchar_t* kWindowTitle = L"VRec";
#endif

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTestConnectionResultMessage = WM_APP + 2;
constexpr UINT kTrayIconId = 1;
constexpr int kTrayMenuOpen = 30001;
constexpr int kTrayMenuExit = 30002;
constexpr UINT kStatusTimerId = 1;

constexpr int kMinWidth = 720;
constexpr int kMinHeight = 520;
constexpr int kDefaultWidth = 860;
constexpr int kDefaultHeight = 560;

const wchar_t* kVirtualHost = L"vrec.local";
const wchar_t* kStartUrl = L"https://vrec.local/index.html";

#ifdef DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kImmersiveDarkMode = DWMWA_USE_IMMERSIVE_DARK_MODE;
#else
constexpr DWORD kImmersiveDarkMode = 20;
#endif

HICON LoadAppIcon(HINSTANCE instance, int size)
{
    const int s = size > 0 ? size : GetSystemMetrics(SM_CXSMICON);
    HICON icon = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, s, s, LR_DEFAULTCOLOR | LR_SHARED));
    if (!icon) icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

} // namespace

WebView2Ui::WebView2Ui(AppHost& host)
    : host_(host)
{
}

WebView2Ui::~WebView2Ui()
{
    CancelTestConnection();
    RemoveTrayIcon();
    if (classRegistered_) {
        UnregisterClassW(kWindowClass, instance_);
    }
    if (backgroundBrush_) {
        DeleteObject(backgroundBrush_);
    }
}

bool WebView2Ui::Create(HINSTANCE instance, int showCommand)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WebView2Ui::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadAppIcon(instance, GetSystemMetrics(SM_CXICON));
    wc.hIconSm = LoadAppIcon(instance, GetSystemMetrics(SM_CXSMICON));
    backgroundBrush_ = CreateSolidBrush(RGB(26, 25, 23));
    if (!backgroundBrush_) {
        return false;
    }
    wc.hbrBackground = backgroundBrush_;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        host_.Log().LogError(
            L"Window class registration failed: " +
            GetLastErrorString(GetLastError()));
        return false;
    }
    classRegistered_ = true;

    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(kDefaultWidth, dpi, 96);
    const int h = MulDiv(kDefaultHeight, dpi, 96);

    // no WS_CAPTION - the title bar is drawn in HTML. WS_THICKFRAME keeps native resize +
    // Win11 snap; the min/max/sysmenu bits keep taskbar animations and the system menu.
    hwnd_ = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, kImmersiveDarkMode, &dark, sizeof(dark));

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);

    InitWebView();
    return true;
}

int WebView2Ui::Run()
{
    MSG msg{};
    for (;;) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == 0) {
            return static_cast<int>(msg.wParam);
        }
        if (result == -1) {
            host_.Log().LogError(
                L"Window message loop failed: " +
                GetLastErrorString(GetLastError()));
            return 1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void WebView2Ui::InitWebView()
{
    const std::filesystem::path webDir = ExecutableDirectory() / L"web";

    const std::filesystem::path localAppData = KnownFolderPath(FOLDERID_LocalAppData);
    const std::filesystem::path userData = localAppData.empty()
        ? std::filesystem::path()
        : localAppData / L"VRec" / L"WebView2";
    const wchar_t* userDataPath = userData.empty() ? nullptr : userData.c_str();

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataPath, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, webDir](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    host_.Log().LogError(
                        L"WebView2 environment creation failed: " +
                        HResultToString(result));
                    return result;
                }
                environment_ = env;
                const HRESULT createControllerResult =
                    env->CreateCoreWebView2Controller(
                    hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, webDir](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(r) || !controller) {
                                host_.Log().LogError(
                                    L"WebView2 controller creation failed: " +
                                    HResultToString(r));
                                return r;
                            }
                            controller_ = controller;
                            const HRESULT coreResult =
                                controller_->get_CoreWebView2(&webview_);
                            if (FAILED(coreResult) || !webview_) {
                                host_.Log().LogError(
                                    L"WebView2 instance lookup failed: " +
                                    HResultToString(coreResult));
                                return coreResult;
                            }

                            ComPtr<ICoreWebView2_3> webview3;
                            if (SUCCEEDED(webview_.As(&webview3))) {
                                const HRESULT mappingResult =
                                    webview3->SetVirtualHostNameToFolderMapping(
                                    kVirtualHost, webDir.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
                                if (FAILED(mappingResult)) {
                                    host_.Log().LogError(
                                        L"WebView2 local content mapping failed: " +
                                        HResultToString(mappingResult));
                                    return mappingResult;
                                }
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webview_->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
#ifdef _DEBUG
                                settings->put_AreDevToolsEnabled(TRUE);
#else
                                settings->put_AreDevToolsEnabled(FALSE);
#endif
                            }

                            EventRegistrationToken token;
                            const HRESULT bridgeResult =
                                webview_->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            HandleWebMessage(raw);
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(),
                                &token);
                            if (FAILED(bridgeResult)) {
                                host_.Log().LogError(
                                    L"WebView2 bridge registration failed: " +
                                    HResultToString(bridgeResult));
                                return bridgeResult;
                            }

                            OnWebViewReady();
                            const HRESULT navigateResult =
                                webview_->Navigate(kStartUrl);
                            if (FAILED(navigateResult)) {
                                host_.Log().LogError(
                                    L"WebView2 navigation failed: " +
                                    HResultToString(navigateResult));
                            }
                            return navigateResult;
                        }).Get());
                if (FAILED(createControllerResult)) {
                    host_.Log().LogError(
                        L"WebView2 controller request failed: " +
                        HResultToString(createControllerResult));
                }
                return createControllerResult;
            }).Get());

    if (FAILED(hr)) {
        MessageBoxW(hwnd_,
            L"Could not start the WebView2 runtime.\n\nInstall the Microsoft Edge WebView2 Runtime and launch VRec again.",
            L"VRec", MB_ICONERROR | MB_OK);
    }
}

void WebView2Ui::OnWebViewReady()
{
    ResizeWebView();
    if (!SetTimer(hwnd_, kStatusTimerId, 1000, nullptr)) {
        host_.Log().LogError(
            L"Status timer creation failed: " +
            GetLastErrorString(GetLastError()));
    }
}

void WebView2Ui::ResizeWebView()
{
    if (!controller_) {
        return;
    }
    RECT rc{};
    if (!GetClientRect(hwnd_, &rc)) {
        host_.Log().LogWarning(
            L"Window bounds lookup failed: " +
            GetLastErrorString(GetLastError()));
        return;
    }
    const HRESULT result = controller_->put_Bounds(rc);
    if (FAILED(result)) {
        host_.Log().LogWarning(
            L"WebView2 resize failed: " +
            HResultToString(result));
    }
}

void WebView2Ui::HandleWebMessage(const std::wstring& json)
{
    WebBridgeCommand command;
    std::wstring error;
    if (!ParseWebBridgeCommand(json, host_.GetSettings(), command, error)) {
        host_.Log().LogWarning(error);
        return;
    }

    switch (command.type) {
    case WebBridgeCommandType::GetSettings:
        PushSettings();
        PushAbout();
        PushStatus();
        break;
    case WebBridgeCommandType::ApplySettings:
        host_.ApplySettings(command.settings);
        break;
    case WebBridgeCommandType::TestConnection:
        StartTestConnection(command.obsSettings);
        break;
    case WebBridgeCommandType::ExportSupportReport: {
        bool ok = false;
        try {
            const std::filesystem::path dir = host_.ExportSupportReport();
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ok = true;
        } catch (...) {
            host_.Log().LogError(L"Support report export failed");
        }
        PostJson(EncodeOperationResult("exportResult", ok));
        break;
    }
    case WebBridgeCommandType::ResetSettings: {
        const bool ok = host_.ResetSettings();
        if (ok) {
            PushSettings();
            PushStatus();
        }
        PostJson(EncodeOperationResult("resetResult", ok));
        break;
    }
    case WebBridgeCommandType::WindowMinimize:
        HandleWindowMinimize();
        break;
    case WebBridgeCommandType::WindowClose:
        HandleWindowClose();
        break;
    case WebBridgeCommandType::WindowDragStart:
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        break;
    case WebBridgeCommandType::OpenRepository:
        ShellExecuteW(nullptr, L"open", L"https://github.com/ESFRick/VRec#connecting-to-obs",
                      nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
}

void WebView2Ui::PushSettings()
{
    PostJson(EncodeSettingsMessage(host_.GetSettings()));
}

void WebView2Ui::PushAbout()
{
    PostJson(EncodeAboutMessage(AppVersionLine(), AppBuildTypeLabel()));
}

void WebView2Ui::PushStatus()
{
    PostJson(EncodeStatusMessage(host_.Status()));
}

void WebView2Ui::PostJson(const std::wstring& json)
{
    if (webview_) {
        const HRESULT result = webview_->PostWebMessageAsJson(json.c_str());
        if (FAILED(result)) {
            host_.Log().LogWarning(
                L"WebView2 message send failed: " +
                HResultToString(result));
        }
    }
}

void WebView2Ui::StartTestConnection(const ObsSettings& settings)
{
    CancelTestConnection();
    testCancel_.store(false);

    std::uint64_t generation = 0;
    {
        std::scoped_lock lock(testResultMutex_);
        generation = ++testGeneration_;
        testResultReady_ = false;
        testResultError_.clear();
    }

    testThread_ = std::thread([this, settings, generation] {
        std::wstring error;
        const bool ok = host_.TestObsConnection(settings, error, &testCancel_);
        if (testCancel_.load()) {
            return;
        }

        {
            std::scoped_lock lock(testResultMutex_);
            if (generation != testGeneration_) {
                return;
            }
            testResultOk_ = ok;
            testResultError_ = std::move(error);
            testResultSettings_ = settings;
            testResultReady_ = true;
        }
        PostMessageW(
            hwnd_,
            kTestConnectionResultMessage,
            static_cast<WPARAM>(generation),
            0);
    });
}

void WebView2Ui::FinishTestConnection(std::uint64_t generation)
{
    {
        std::scoped_lock lock(testResultMutex_);
        if (generation != testGeneration_) {
            return;
        }
    }
    if (testThread_.joinable()) {
        testThread_.join();
    }

    bool ready = false;
    bool ok = false;
    std::wstring error;
    ObsSettings settings;
    {
        std::scoped_lock lock(testResultMutex_);
        ready = testResultReady_;
        ok = testResultOk_;
        error = testResultError_;
        settings = testResultSettings_;
        testResultReady_ = false;
    }
    if (!ready) {
        return;
    }

    if (ok && !host_.ConfirmObsSettings(settings)) {
        ok = false;
        error = L"Could not save OBS settings";
    }
    if (ok) {
        PushSettings();
        PushStatus();
    }
    PostJson(EncodeOperationResult("testResult", ok, error));
}

void WebView2Ui::CancelTestConnection()
{
    testCancel_.store(true);
    if (testThread_.joinable()) {
        testThread_.join();
    }
    std::scoped_lock lock(testResultMutex_);
    ++testGeneration_;
    testResultReady_ = false;
    testResultError_.clear();
}

bool WebView2Ui::CloseToTrayEnabled() const
{
    return host_.GetSettings().advanced.closeToTray;
}

void WebView2Ui::HandleWindowMinimize()
{
    if (!hwnd_) {
        return;
    }
    ShowWindow(hwnd_, SW_MINIMIZE);
}

void WebView2Ui::HandleWindowClose()
{
    if (CloseToTrayEnabled()) {
        HideToTray();
        return;
    }
    ExitApplication();
}

void WebView2Ui::AddTrayIcon()
{
    if (!hwnd_ || trayIconAdded_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadAppIcon(instance_, GetSystemMetrics(SM_CXSMICON));
    wcscpy_s(data.szTip, AppVersionLine().c_str());
    if (Shell_NotifyIconW(NIM_ADD, &data)) {
        trayIconAdded_ = true;
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void WebView2Ui::RemoveTrayIcon()
{
    if (!hwnd_ || !trayIconAdded_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    trayIconAdded_ = false;
}

void WebView2Ui::ShowTrayMinimizedNotification()
{
    if (!hwnd_ || !trayIconAdded_) {
        return;
    }
    const bool russian = host_.GetSettings().language == Language::Russian;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    wcscpy_s(data.szInfoTitle, L"VRec");
    wcscpy_s(data.szInfo, russian ? L"VRec свернут в трей" : L"VRec minimized to Windows Tray");
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void WebView2Ui::HideToTray()
{
    if (!hwnd_) {
        return;
    }
    if (!trayIconAdded_) {
        AddTrayIcon();
    }
    ShowWindow(hwnd_, SW_HIDE);
    ShowTrayMinimizedNotification();
}

void WebView2Ui::ShowFromTray()
{
    if (!hwnd_) {
        return;
    }
    ShowWindow(hwnd_, SW_SHOW);
    if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
    }
    // A tray activation comes from a different foreground thread on some systems.
    HWND fg = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myTid = GetCurrentThreadId();
    if (fgTid && fgTid != myTid) {
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(hwnd_);
        SetActiveWindow(hwnd_);
        AttachThreadInput(myTid, fgTid, FALSE);
    } else {
        SetForegroundWindow(hwnd_);
        SetActiveWindow(hwnd_);
    }
}

void WebView2Ui::ShowTrayMenu()
{
    if (!hwnd_) {
        return;
    }
    POINT point{};
    GetCursorPos(&point);

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    const bool russian = host_.GetSettings().language == Language::Russian;
    AppendMenuW(menu, MF_STRING, kTrayMenuOpen, russian ? L"Открыть VRec" : L"Open VRec");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayMenuExit, russian ? L"Выход" : L"Exit");

    SetForegroundWindow(hwnd_);
    const int command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (command == kTrayMenuOpen) {
        ShowFromTray();
    } else if (command == kTrayMenuExit) {
        ExitApplication();
    }
}

void WebView2Ui::ExitApplication()
{
    exiting_ = true;
    host_.Shutdown();
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK WebView2Ui::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WebView2Ui* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<WebView2Ui*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WebView2Ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT WebView2Ui::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const RECT requested = params->rgrc[0];
            DefWindowProcW(hwnd_, message, wParam, lParam);
            params->rgrc[0].top = requested.top;
            return 0;
        }
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        ResizeWebView();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
        const UINT use = dpi ? dpi : 96;
        mmi->ptMinTrackSize.x = MulDiv(kMinWidth, use, 96);
        mmi->ptMinTrackSize.y = MulDiv(kMinHeight, use, 96);
        return 0;
    }

    case WM_DPICHANGED: {
        auto* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kStatusTimerId) {
            PushStatus();
        }
        return 0;

    case kTestConnectionResultMessage:
        FinishTestConnection(static_cast<std::uint64_t>(wParam));
        return 0;

    case kTrayMessage: {
        const UINT event = LOWORD(lParam);
        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK || event == NIN_SELECT || event == NIN_KEYSELECT) {
            ShowFromTray();
        } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            ShowTrayMenu();
        }
        return 0;
    }

    case WM_CLOSE:
        if (exiting_) {
            ExitApplication();
        } else {
            HandleWindowClose();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kStatusTimerId);
        CancelTestConnection();
        RemoveTrayIcon();
        if (controller_) {
            controller_->Close();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
