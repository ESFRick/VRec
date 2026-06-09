#include "OverlayRenderer.h"

#include "Localization.h"
#include "TextUtil.h"

#include <Windows.h>
#include <openvr.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace color {
constexpr COLORREF Panel = RGB(28, 26, 24);
constexpr COLORREF PanelEdge = RGB(53, 50, 47);
constexpr COLORREF TextHigh = RGB(234, 228, 219);
constexpr COLORREF TextMid = RGB(157, 149, 138);
constexpr COLORREF TextDim = RGB(107, 100, 90);
constexpr COLORREF Accent = RGB(214, 143, 87);
constexpr COLORREF Ready = RGB(111, 174, 126);
constexpr COLORREF Warn = RGB(199, 154, 78);
constexpr COLORREF WarnSoft = RGB(59, 49, 34);
constexpr COLORREF Danger = RGB(224, 122, 112);
constexpr COLORREF Record = RGB(216, 66, 58);
constexpr COLORREF RecordInk = RGB(255, 255, 255);
constexpr COLORREF RecordSoft = RGB(58, 32, 29);
constexpr COLORREF Inset = RGB(19, 18, 16);
constexpr COLORREF Border = RGB(61, 58, 54);
} // namespace color

RECT GearButtonRect()
{
    return RECT{ 458, 14, 492, 48 };
}

RECT SettingsBackButtonRect()
{
    return RECT{ 20, 16, 54, 50 };
}

RECT HideAngleMinusRect()
{
    return RECT{ 32, 118, 76, 162 };
}

RECT HideAnglePlusRect()
{
    return RECT{ 436, 118, 480, 162 };
}

RECT HideAngleSliderRect()
{
    return RECT{ 96, 124, 416, 156 };
}

RECT HideAngleResetRect()
{
    return RECT{ 352, 192, 480, 230 };
}

enum class PanelState {
    Ready,
    Recording,
    Connecting,
    Offline,
    Error
};

class GdiObject {
public:
    explicit GdiObject(HGDIOBJ handle = nullptr)
        : handle_(handle)
    {
    }

    ~GdiObject()
    {
        if (handle_) {
            DeleteObject(handle_);
        }
    }

    GdiObject(const GdiObject&) = delete;
    GdiObject& operator=(const GdiObject&) = delete;

    GdiObject(GdiObject&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    GdiObject& operator=(GdiObject&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    HGDIOBJ get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset(HGDIOBJ handle = nullptr)
    {
        if (handle_ && handle_ != handle) {
            DeleteObject(handle_);
        }
        handle_ = handle;
    }

private:
    HGDIOBJ handle_ = nullptr;
};

class ScreenDc {
public:
    ScreenDc()
        : dc_(GetDC(nullptr))
    {
    }

    ~ScreenDc()
    {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }

    HDC get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
};

class MemoryDc {
public:
    explicit MemoryDc(HDC compatibleWith)
        : dc_(CreateCompatibleDC(compatibleWith))
    {
    }

    ~MemoryDc()
    {
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    HDC get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
};

class DcSelection {
public:
    DcSelection(HDC dc, HGDIOBJ object)
        : dc_(dc)
        , previous_(object ? SelectObject(dc, object) : nullptr)
    {
    }

    ~DcSelection()
    {
        if (previous_ && previous_ != HGDI_ERROR) {
            SelectObject(dc_, previous_);
        }
    }

    DcSelection(const DcSelection&) = delete;
    DcSelection& operator=(const DcSelection&) = delete;

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

COLORREF Blend(COLORREF base, COLORREF overlay, double amount)
{
    const auto channel = [amount](int from, int to) {
        return static_cast<int>(from + (to - from) * amount + 0.5);
    };
    return RGB(
        channel(GetRValue(base), GetRValue(overlay)),
        channel(GetGValue(base), GetGValue(overlay)),
        channel(GetBValue(base), GetBValue(overlay)));
}

std::wstring OverlayErrorToString(vr::EVROverlayError error)
{
    const char* name = vr::VROverlay()
        ? vr::VROverlay()->GetOverlayErrorNameFromEnum(error)
        : nullptr;
    std::stringstream stream;
    stream << (name ? name : "VROverlayError")
           << " (" << static_cast<int>(error) << ")";
    return Utf8ToWide(stream.str());
}

void LogOverlayError(
    Diagnostics* diagnostics,
    const wchar_t* action,
    vr::EVROverlayError error)
{
    if (diagnostics && error != vr::VROverlayError_None) {
        diagnostics->LogWarning(
            std::wstring(action) + L" failed: " +
            OverlayErrorToString(error));
    }
}

void DrawRoundedRect(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF fill,
    COLORREF border,
    int borderWidth)
{
    GdiObject brush(CreateSolidBrush(fill));
    GdiObject pen(borderWidth > 0
        ? CreatePen(PS_SOLID, borderWidth, border)
        : nullptr);
    DcSelection brushSelection(dc, brush.get());
    DcSelection penSelection(
        dc,
        pen ? pen.get() : GetStockObject(NULL_PEN));
    RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius);
}

void DrawDebugChecker(HDC dc)
{
    constexpr int cellSize = 32;
    GdiObject magenta(CreateSolidBrush(RGB(255, 0, 255)));
    GdiObject green(CreateSolidBrush(RGB(0, 255, 64)));
    for (int y = 0; y < OverlayRenderer::Height; y += cellSize) {
        for (int x = 0; x < OverlayRenderer::Width; x += cellSize) {
            RECT cell{
                x,
                y,
                std::min(x + cellSize, OverlayRenderer::Width),
                std::min(y + cellSize, OverlayRenderer::Height),
            };
            FillRect(
                dc,
                &cell,
                reinterpret_cast<HBRUSH>(
                    ((x / cellSize) + (y / cellSize)) % 2 == 0
                        ? magenta.get()
                        : green.get()));
        }
    }
}

GdiObject MakeFont(int height, int weight, const wchar_t* family)
{
    return GdiObject(CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        family));
}

PanelState GetPanelState(const StatusSnapshot& status)
{
    if (status.recorderState == RecorderState::Recording) {
        return PanelState::Recording;
    }
    if (status.obsConnState == ObsConnState::Connected) {
        return PanelState::Ready;
    }
    if (status.obsConnState == ObsConnState::Connecting) {
        return PanelState::Connecting;
    }
    if (!status.lastError.empty()) {
        return PanelState::Error;
    }
    return PanelState::Offline;
}

bool DrawRecordingPanel(
    HDC dc,
    const StatusSnapshot& status,
    Language language,
    Diagnostics* diagnostics)
{
    GdiObject titleFont = MakeFont(-21, FW_BOLD, L"Segoe UI");
    GdiObject chipFont = MakeFont(-15, FW_SEMIBOLD, L"Segoe UI");
    GdiObject buttonLargeFont = MakeFont(
        OverlayRenderer::ButtonFontHeight,
        FW_BOLD,
        L"Segoe UI");
    GdiObject buttonMediumFont = MakeFont(-23, FW_BOLD, L"Segoe UI");
    GdiObject hintFont = MakeFont(-14, FW_NORMAL, L"Segoe UI");
    if (!titleFont || !chipFont ||
        !buttonLargeFont || !buttonMediumFont || !hintFont) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay font creation failed");
        }
        return false;
    }

    const PanelState panel = GetPanelState(status);
    const bool russian = language == Language::Russian;
    const bool recording = panel == PanelState::Recording;
    const bool showHint =
        panel == PanelState::Offline || panel == PanelState::Error;

    DrawRoundedRect(
        dc,
        RECT{0, 0, OverlayRenderer::Width, OverlayRenderer::Height},
        18,
        color::Panel,
        color::Panel,
        0);
    {
        DcSelection brushSelection(dc, GetStockObject(NULL_BRUSH));
        GdiObject border(CreatePen(PS_SOLID, 1, color::PanelEdge));
        DcSelection penSelection(dc, border.get());
        RoundRect(
            dc,
            1,
            1,
            OverlayRenderer::Width - 1,
            OverlayRenderer::Height - 1,
            18,
            18);
    }

    SetBkMode(dc, TRANSPARENT);
    DcSelection fontSelection(dc, titleFont.get());

    SIZE vSize{};
    GetTextExtentPoint32W(dc, L"V", 1, &vSize);
    RECT brand{22, 16, 220, 48};
    SetTextColor(dc, color::Accent);
    DrawTextW(dc, L"V", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    brand.left += vSize.cx;
    SetTextColor(dc, color::TextHigh);
    DrawTextW(dc, L"Rec", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    constexpr int chipRight = 446;
    constexpr int chipCenterY = 32;
    constexpr int chipHeight = 26;
    constexpr int chipPadding = 11;
    constexpr int chipTop = chipCenterY - chipHeight / 2;
    constexpr int chipBottom = chipTop + chipHeight;

    if (recording) {
        const wchar_t* recordText =
            russian ? L"\u25CF \u0417\u0430\u043F\u0438\u0441\u044C" : L"\u25CF Recording";

        SIZE recordSize{};
        SelectObject(dc, chipFont.get());
        GetTextExtentPoint32W(
            dc,
            recordText,
            static_cast<int>(wcslen(recordText)),
            &recordSize);
        const int chipLeft = chipRight - (recordSize.cx + chipPadding * 2);

        DrawRoundedRect(
            dc,
            RECT{chipLeft, chipTop, chipRight, chipBottom},
            8,
            color::RecordSoft,
            color::RecordSoft,
            0);
        SetTextColor(dc, color::Record);
        RECT recordRect{
            chipLeft + chipPadding,
            chipTop,
            chipRight - chipPadding,
            chipBottom
        };
        SelectObject(dc, chipFont.get());
        DrawTextW(
            dc,
            recordText,
            -1,
            &recordRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF tone = color::TextMid;
        const wchar_t* label = russian ? L"● Нет OBS" : L"● No OBS";
        switch (panel) {
        case PanelState::Ready:
            tone = color::Ready;
            label = russian ? L"● Готов" : L"● Ready";
            break;
        case PanelState::Connecting:
            tone = color::Warn;
            label = russian ? L"● Подключение" : L"● Connecting";
            break;
        case PanelState::Error:
            tone = color::Danger;
            label = russian ? L"● Ошибка OBS" : L"● OBS error";
            break;
        default:
            break;
        }

        SelectObject(dc, chipFont.get());
        SIZE size{};
        GetTextExtentPoint32W(
            dc,
            label,
            static_cast<int>(wcslen(label)),
            &size);
        const int chipLeft = chipRight - (size.cx + chipPadding * 2);
        DrawRoundedRect(
            dc,
            RECT{chipLeft, chipTop, chipRight, chipBottom},
            8,
            color::Panel,
            Blend(color::Panel, tone, 0.45),
            1);
        SetTextColor(dc, tone);
        RECT labelRect{
            chipLeft + chipPadding,
            chipTop,
            chipRight - chipPadding,
            chipBottom,
        };
        DrawTextW(
            dc,
            label,
            -1,
            &labelRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT buttonRect{22, 62, 490, showHint ? 200 : 236};
    COLORREF buttonFill = color::Inset;
    COLORREF buttonBorder = color::Border;
    COLORREF buttonText = color::TextDim;
    int buttonBorderWidth = 1;
    HGDIOBJ buttonFont = buttonMediumFont.get();
    std::wstring buttonLabel;

    switch (panel) {
    case PanelState::Ready:
        buttonFill = color::Record;
        buttonBorder = color::Record;
        buttonBorderWidth = 0;
        buttonText = color::RecordInk;
        buttonFont = buttonLargeFont.get();
        buttonLabel = std::wstring(L"●  ") + Tr(language, TextId::OverlayRec);
        break;
    case PanelState::Recording:
        buttonFill = color::RecordSoft;
        buttonBorder = color::Record;
        buttonBorderWidth = 2;
        buttonText = color::Record;
        buttonFont = buttonLargeFont.get();
        buttonLabel = std::wstring(L"■  ") + Tr(language, TextId::OverlayStop);
        break;
    case PanelState::Connecting:
        buttonLabel = russian ? L"Подключение" : L"Connecting";
        break;
    case PanelState::Error:
        buttonFill = color::WarnSoft;
        buttonBorder = color::Warn;
        buttonText = color::Warn;
        buttonLabel = russian ? L"Нет связи с OBS" : L"OBS offline";
        break;
    default:
        buttonLabel = russian ? L"Нет связи с OBS" : L"OBS offline";
        break;
    }

    DrawRoundedRect(
        dc,
        buttonRect,
        14,
        buttonFill,
        buttonBorder,
        buttonBorderWidth);
    SelectObject(dc, buttonFont);
    SetTextColor(dc, buttonText);
    SetTextCharacterExtra(
        dc,
        panel == PanelState::Ready || panel == PanelState::Recording
            ? OverlayRenderer::ButtonLetterSpacing
            : 1);
    RECT mutableButtonRect = buttonRect;
    DrawTextW(
        dc,
        buttonLabel.c_str(),
        -1,
        &mutableButtonRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextCharacterExtra(dc, 0);

    if (showHint) {
        const wchar_t* hint = nullptr;
        if (panel == PanelState::Error) {
            hint = russian
                ? L"Не удалось подключиться к OBS. Проверьте в приложении."
                : L"OBS connection failed. Check it in the desktop app.";
        } else {
            hint = russian
                ? L"Подключитесь к OBS в приложении, чтобы записывать."
                : L"Connect to OBS in the desktop app to record.";
        }
        SelectObject(dc, hintFont.get());
        SetTextColor(dc, color::TextDim);
        RECT hintRect{22, 206, 490, 238};
        DrawTextW(
            dc,
            hint,
            -1,
            &hintRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    const RECT gear = GearButtonRect();
    DrawRoundedRect(dc, gear, 8, color::Panel, color::PanelEdge, 1);

    GdiObject gearFont = MakeFont(-24, FW_NORMAL, L"Segoe UI Symbol");
    if (!gearFont) {
        gearFont = MakeFont(-24, FW_NORMAL, L"Segoe UI");
    }
    if (gearFont) {
        SelectObject(dc, gearFont.get());
        SetTextColor(dc, color::TextMid);
        RECT gearGlyph{ gear.left, gear.top - 1, gear.right, gear.bottom - 1 };
        DrawTextW(
            dc,
            L"\u2699",
            -1,
            &gearGlyph,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    return true;
}

void DrawSlider(HDC dc, const RECT& track, int value)
{
    constexpr int trackHeight = 6;
    const int centerY = (track.top + track.bottom) / 2;
    const int trackLeft = track.left + 10;
    const int trackRight = track.right - 10;
    const int range = kOverlayHideAngleMaxDegrees - kOverlayHideAngleMinDegrees;
    const double t = static_cast<double>(value - kOverlayHideAngleMinDegrees) / static_cast<double>(range);
    const int knobX = static_cast<int>(trackLeft + (trackRight - trackLeft) * std::clamp(t, 0.0, 1.0) + 0.5);

    DrawRoundedRect(
        dc,
        RECT{ trackLeft, centerY - trackHeight / 2, trackRight, centerY + trackHeight / 2 },
        5,
        color::Inset,
        color::Border,
        1);
    DrawRoundedRect(
        dc,
        RECT{ trackLeft, centerY - trackHeight / 2, knobX, centerY + trackHeight / 2 },
        5,
        color::Accent,
        color::Accent,
        0);
    DrawRoundedRect(
        dc,
        RECT{ knobX - 8, centerY - 13, knobX + 8, centerY + 13 },
        8,
        color::TextHigh,
        color::TextHigh,
        0);
}

bool DrawSettingsPanel(
    HDC dc,
    const Settings& settings,
    Diagnostics* diagnostics)
{
    GdiObject titleFont = MakeFont(-21, FW_BOLD, L"Segoe UI");
    GdiObject labelFont = MakeFont(-20, FW_SEMIBOLD, L"Segoe UI");
    GdiObject valueFont = MakeFont(-28, FW_BOLD, L"Segoe UI");
    GdiObject buttonFont = MakeFont(-18, FW_SEMIBOLD, L"Segoe UI");
    if (!titleFont || !labelFont || !valueFont || !buttonFont) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay settings font creation failed");
        }
        return false;
    }

    const bool russian = settings.language == Language::Russian;
    DrawRoundedRect(
        dc,
        RECT{0, 0, OverlayRenderer::Width, OverlayRenderer::Height},
        18,
        color::Panel,
        color::Panel,
        0);
    {
        DcSelection brushSelection(dc, GetStockObject(NULL_BRUSH));
        GdiObject border(CreatePen(PS_SOLID, 1, color::PanelEdge));
        DcSelection penSelection(dc, border.get());
        RoundRect(
            dc,
            1,
            1,
            OverlayRenderer::Width - 1,
            OverlayRenderer::Height - 1,
            18,
            18);
    }

    SetBkMode(dc, TRANSPARENT);

    const RECT back = SettingsBackButtonRect();
    DrawRoundedRect(dc, back, 8, color::Panel, color::PanelEdge, 1);
    GdiObject backPen(CreatePen(PS_SOLID, 2, color::TextMid));
    DcSelection backPenSelection(dc, backPen.get());
    MoveToEx(dc, back.left + 21, back.top + 10, nullptr);
    LineTo(dc, back.left + 11, back.top + 17);
    LineTo(dc, back.left + 21, back.top + 24);
    MoveToEx(dc, back.left + 12, back.top + 17, nullptr);
    LineTo(dc, back.right - 10, back.top + 17);

    SelectObject(dc, titleFont.get());
    SetTextColor(dc, color::TextHigh);
    RECT titleRect{ 66, 16, 320, 50 };
    DrawTextW(
        dc,
        russian ? L"Настройки" : L"Settings",
        -1,
        &titleRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, labelFont.get());
    SetTextColor(dc, color::TextMid);
    RECT labelRect{ 32, 78, 250, 110 };
    DrawTextW(
        dc,
        russian ? L"Угол скрытия" : L"Hide angle",
        -1,
        &labelRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, valueFont.get());
    SetTextColor(dc, color::Accent);
    std::wstring value = std::to_wstring(settings.overlay.hideAngleDegrees) + L"°";
    RECT valueRect{ 320, 74, 480, 112 };
    DrawTextW(
        dc,
        value.c_str(),
        -1,
        &valueRect,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const RECT minus = HideAngleMinusRect();
    const RECT plus = HideAnglePlusRect();
    DrawRoundedRect(dc, minus, 10, color::Inset, color::Border, 1);
    DrawRoundedRect(dc, plus, 10, color::Inset, color::Border, 1);
    SelectObject(dc, valueFont.get());
    SetTextColor(dc, color::TextHigh);
    RECT minusText = minus;
    RECT plusText = plus;
    DrawTextW(dc, L"−", -1, &minusText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"+", -1, &plusText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawSlider(dc, HideAngleSliderRect(), settings.overlay.hideAngleDegrees);

    const RECT reset = HideAngleResetRect();
    DrawRoundedRect(dc, reset, 9, color::Panel, color::PanelEdge, 1);
    SelectObject(dc, buttonFont.get());
    SetTextColor(dc, color::TextHigh);
    RECT resetText = reset;
    DrawTextW(
        dc,
        russian ? L"Сброс" : L"Reset",
        -1,
        &resetText,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    return true;
}

bool DrawPanel(
    HDC dc,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    Diagnostics* diagnostics)
{
    if (panelPage == OverlayPanelPage::Settings) {
        return DrawSettingsPanel(dc, settings, diagnostics);
    }
    return DrawRecordingPanel(dc, status, settings.language, diagnostics);
}

bool RenderPixels(
    std::vector<std::uint8_t>& output,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    bool debugChecker,
    Diagnostics* diagnostics)
{
    ScreenDc screen;
    if (!screen) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay screen DC creation failed");
        }
        return false;
    }
    MemoryDc dc(screen.get());
    if (!dc) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay memory DC creation failed");
        }
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = OverlayRenderer::Width;
    info.bmiHeader.biHeight = -OverlayRenderer::Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    GdiObject bitmap(CreateDIBSection(
        screen.get(),
        &info,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0));
    if (!bitmap || !bits) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Overlay DIB creation failed");
        }
        return false;
    }
    DcSelection bitmapSelection(dc.get(), bitmap.get());
    std::memset(
        bits,
        0,
        static_cast<size_t>(OverlayRenderer::Width) *
            OverlayRenderer::Height * 4);

    if (debugChecker) {
        DrawDebugChecker(dc.get());
    } else if (!DrawPanel(dc.get(), status, settings, panelPage, diagnostics)) {
        return false;
    }

    const auto* source = static_cast<const std::uint8_t*>(bits);
    output.resize(
        static_cast<size_t>(OverlayRenderer::Width) *
        OverlayRenderer::Height * 4);
    for (size_t pixel = 0;
         pixel < static_cast<size_t>(OverlayRenderer::Width) *
             OverlayRenderer::Height;
         ++pixel) {
        const std::uint8_t blue = source[pixel * 4];
        const std::uint8_t green = source[pixel * 4 + 1];
        const std::uint8_t red = source[pixel * 4 + 2];
        output[pixel * 4] = red;
        output[pixel * 4 + 1] = green;
        output[pixel * 4 + 2] = blue;
        output[pixel * 4 + 3] = (red | green | blue) ? 255 : 0;
    }
    return true;
}

} // namespace

struct OverlayRenderer::Impl {
    std::vector<std::uint8_t> pixels;
    vr::EVROverlayError lastRawError = vr::VROverlayError_None;
};

OverlayRenderer::OverlayRenderer()
    : impl_(std::make_unique<Impl>())
{
}

OverlayRenderer::~OverlayRenderer() = default;

bool OverlayRenderer::Render(
    std::uint64_t overlayHandle,
    const StatusSnapshot& status,
    const Settings& settings,
    OverlayPanelPage panelPage,
    bool debugChecker,
    Diagnostics* diagnostics)
{
    if (!vr::VROverlay() ||
        overlayHandle == vr::k_ulOverlayHandleInvalid ||
        overlayHandle == 0) {
        return false;
    }
    if (!RenderPixels(
            impl_->pixels,
            status,
            settings,
            panelPage,
            debugChecker,
            diagnostics)) {
        return false;
    }
    const auto clearError =
        vr::VROverlay()->ClearOverlayTexture(overlayHandle);
    if (clearError != vr::VROverlayError_None) {
        LogOverlayError(
            diagnostics,
            L"ClearOverlayTexture",
            clearError);
    }
    const auto error = vr::VROverlay()->SetOverlayRaw(
        overlayHandle,
        impl_->pixels.data(),
        Width,
        Height,
        4);
    if (error != vr::VROverlayError_None) {
        if (error != impl_->lastRawError) {
            LogOverlayError(diagnostics, L"SetOverlayRaw", error);
        }
        impl_->lastRawError = error;
        return false;
    }
    impl_->lastRawError = vr::VROverlayError_None;
    return true;
}

void OverlayRenderer::Reset()
{
    impl_->pixels.clear();
    impl_->lastRawError = vr::VROverlayError_None;
}
