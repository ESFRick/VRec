#include "../src/Diagnostics.h"
#include "../src/Localization.h"
#include "../src/ObsClient.h"
#include "../src/ObsProtocol.h"
#include "../src/OverlayRenderer.h"
#include "../src/OverlayVisualState.h"
#include "../src/SettingsStore.h"
#include "../src/WebBridgeCodec.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const wchar_t* message)
{
    if (condition) {
        return;
    }
    std::wcerr << L"FAIL: " << message << L"\n";
    ++failures;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
}

std::filesystem::path ProjectRoot()
{
    const std::filesystem::path current = std::filesystem::current_path();
    if (std::filesystem::exists(current / L"web" / L"styles.css")) {
        return current;
    }

    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        executable,
        static_cast<DWORD>(std::size(executable)));
    if (length == 0 || length == std::size(executable)) {
        return current;
    }

    std::filesystem::path root =
        std::filesystem::path(executable).parent_path();
    for (int level = 0; level < 4; ++level) {
        root = root.parent_path();
    }
    return root;
}

void TestSettingsRoundTrip(const std::filesystem::path& path, Diagnostics& log)
{
    SettingsStore store(path, &log);
    Settings settings;
    settings.language = Language::Russian;
    settings.obs.host = L"studio-🟢.local";
    settings.obs.port = 4466;
    settings.obs.password = L"line one\n\"quoted\"\\tail";
    settings.obsConfigured = true;
    settings.overlay.hand = Hand::Left;
    settings.overlay.placement = OverlayPlacement::UnderController;
    settings.advanced.logLevel = L"debug";

    Check(store.Save(settings), L"settings save succeeds");

    const Settings loaded = store.Load();
    Check(loaded.version == 3, L"settings use schema version 3");
    Check(loaded.language == Language::Russian, L"language round-trips");
    Check(loaded.obs.host == settings.obs.host, L"Unicode OBS host round-trips");
    Check(loaded.obs.port == 4466, L"OBS port round-trips");
    Check(loaded.obs.password == settings.obs.password, L"escaped OBS password round-trips");
    Check(loaded.obsConfigured, L"OBS opt-in round-trips");
    Check(loaded.overlay.hand == Hand::Left, L"overlay hand round-trips");
    Check(loaded.overlay.placement == OverlayPlacement::UnderController, L"overlay placement round-trips");
    Check(loaded.advanced.logLevel == L"debug", L"log level round-trips");

    const std::string json = ReadFile(path);
    Check(json.find("\"version\": 3") != std::string::npos, L"schema version is persisted");
    Check(json.find("\"obsConfigured\": true") != std::string::npos, L"OBS opt-in is persisted");
    Check(json.find("outputFolder") == std::string::npos, L"legacy output folder is not persisted");
    Check(json.find("autoLaunch") == std::string::npos, L"legacy auto-launch is not persisted");
    Check(json.find("overlayScale") == std::string::npos, L"legacy overlay scale is not persisted");
    Check(json.find("overlayOffset") == std::string::npos, L"legacy overlay offsets are not persisted");
    Check(json.find("obsAutoConnect") == std::string::npos, L"legacy OBS auto-connect is not persisted");
}

void TestSettingsMigration(const std::filesystem::path& path, Diagnostics& log)
{
    WriteFile(
        path,
        R"({
  "version": 2,
  "language": "ru",
  "obsHost": "legacy.local",
  "obsPort": 4457,
  "obsPassword": "plain-text",
  "overlayHand": "left",
  "overlayPlacement": "underController",
  "outputFolder": "C:\\ignored",
  "autoLaunch": true,
  "overlayScale": 1.4,
  "unknownFutureField": {"host": "wrong.local"}
})");

    SettingsStore store(path, &log);
    const Settings loaded = store.Load();
    Check(loaded.version == 3, L"legacy settings migrate to schema version 3");
    Check(loaded.obs.host == L"legacy.local", L"legacy OBS host is loaded");
    Check(loaded.obs.password == L"plain-text", L"legacy plaintext password is loaded");
    Check(loaded.obsConfigured, L"legacy OBS settings enable reconnect");
    Check(loaded.overlay.hand == Hand::Left, L"legacy hand is loaded");
    Check(loaded.overlay.placement == OverlayPlacement::UnderController, L"legacy placement is loaded");

    Check(store.Save(loaded), L"migrated settings save succeeds");
    const std::string migrated = ReadFile(path);
    Check(migrated.find("legacy.local") != std::string::npos, L"migrated host is retained");
    Check(migrated.find("unknownFutureField") == std::string::npos, L"unknown legacy fields are not rewritten");
}

void TestFreshSettingsDoNotConnect(const std::filesystem::path& path, Diagnostics& log)
{
    SettingsStore store(path, &log);
    const Settings loaded = store.Load();
    Check(!loaded.obsConfigured, L"fresh settings do not enable OBS reconnect");

    WriteFile(
        path,
        R"({
  "version": 3,
  "obsHost": "127.0.0.1",
  "obsPort": 4455,
  "obsPassword": "",
  "obsConfigured": false
})");
    Check(!store.Load().obsConfigured, L"explicit OBS opt-out is preserved");
}

void TestMalformedSettings(const std::filesystem::path& path, Diagnostics& log)
{
    WriteFile(path, R"({"obsHost": [})");
    SettingsStore store(path, &log);
    const Settings loaded = store.Load();
    Check(loaded.version == 3, L"malformed settings fall back to schema version 3 defaults");
    Check(loaded.obs.host == L"127.0.0.1", L"malformed settings use default host");
}

void TestWebBridgeCodec()
{
    Settings current;
    current.obsConfigured = true;
    WebBridgeCommand command;
    std::wstring error;
    const std::wstring message =
        LR"({"type":"applySettings","host":"wrong.local","payload":{"language":"ru","obs":{"host":"nested.local","port":4460,"password":"p\"w"},"overlay":{"hand":"left","placement":"underController"},"advanced":{"logLevel":"debug"}}})";

    Check(ParseWebBridgeCommand(message, current, command, error), L"valid bridge command parses");
    Check(command.type == WebBridgeCommandType::ApplySettings, L"bridge command type is applySettings");
    Check(command.settings.obs.host == L"nested.local", L"bridge parser respects object nesting");
    Check(command.settings.obs.password == L"p\"w", L"bridge parser unescapes strings");
    Check(!command.settings.obsConfigured, L"changed OBS settings require a new connection test");
    Check(command.settings.language == Language::Russian, L"bridge parser reads language");
    Check(command.settings.overlay.hand == Hand::Left, L"bridge parser reads hand");
    Check(command.settings.advanced.logLevel == L"debug", L"bridge parser reads log level");

    Check(!ParseWebBridgeCommand(L"{broken", current, command, error), L"malformed bridge JSON is rejected");
    Check(!error.empty(), L"malformed bridge JSON reports an error");
    Check(!ParseWebBridgeCommand(LR"({"type":"unknown","payload":null})", current, command, error), L"unknown bridge command is rejected");

    StatusSnapshot status;
    status.obsConnState = ObsConnState::Connected;
    status.recorderState = RecorderState::Recording;
    status.recordingTime = std::chrono::seconds(65);
    status.lastError = L"quote: \"";
    status.logLines = { L"one", L"два" };
    const std::wstring encoded = EncodeStatusMessage(status);
    Check(encoded.find(LR"("recordingSeconds":65)") != std::wstring::npos, L"status encoder writes recording time");
    Check(encoded.find(LR"(quote: \")") != std::wstring::npos, L"status encoder escapes quotes");
    Check(encoded.find(L"два") != std::wstring::npos, L"status encoder preserves Unicode");
}

void TestObsProtocol()
{
    ObsHello hello;
    Check(
        ParseObsHello(
            R"({"op":0,"d":{"rpcVersion":1,"authentication":{"challenge":"challenge","salt":"salt"}}})",
            hello),
        L"OBS Hello parses");
    Check(hello.requiresAuthentication, L"OBS Hello detects authentication");
    Check(hello.salt == "salt" && hello.challenge == "challenge", L"OBS Hello reads auth fields");

    const std::string auth = BuildObsAuthentication("p@ss\"word", "salt", "challenge");
    Check(auth == "80znhPCQ0HyrV6SvYYw6+nFRykTXDJsa/4kMVAoo6u8=", L"OBS authentication matches protocol formula");

    const std::string identify = BuildObsIdentify("p@ss\"word", hello);
    Check(identify.find("\"op\":1") != std::string::npos, L"OBS Identify has correct opcode");
    Check(identify.find(auth) != std::string::npos, L"OBS Identify contains authentication");

    const std::string request = BuildObsRequest("StartRecord", 42);
    Check(request.find("\"requestType\":\"StartRecord\"") != std::string::npos, L"OBS request contains request type");
    Check(request.find("\"requestId\":\"42\"") != std::string::npos, L"OBS request contains request id");

    ObsRecordingUpdate update;
    Check(
        ParseObsRecordingUpdate(
            R"({"op":5,"d":{"eventType":"RecordStateChanged","eventData":{"outputActive":true}}})",
            update),
        L"OBS recording event parses");
    Check(update.active && !update.hasDuration, L"OBS recording event updates active state");

    Check(
        ParseObsRecordingUpdate(
            R"({"op":7,"d":{"requestType":"GetRecordStatus","requestStatus":{"result":true},"responseData":{"outputActive":true,"outputDuration":1234}}})",
            update),
        L"OBS GetRecordStatus response parses");
    Check(update.active && update.hasDuration && update.duration == std::chrono::milliseconds(1234), L"OBS status reads nested duration");

    ObsRequestResult result;
    Check(
        ParseObsRequestResult(
            R"({"op":7,"d":{"requestType":"StartRecord","requestId":"7","requestStatus":{"result":true,"code":100}}})",
            result),
        L"OBS command response parses");
    Check(
        result.requestType == "StartRecord" && result.success,
        L"OBS command success is preserved");

    Check(
        ParseObsRequestResult(
            R"({"op":7,"d":{"requestType":"StopRecord","requestId":"8","requestStatus":{"result":false,"code":501,"comment":"recording is not active"}}})",
            result),
        L"OBS command failure parses");
    Check(
        !result.success && result.code == 501 &&
            result.comment == "recording is not active",
        L"OBS command failure details are preserved");

    Check(!ParseObsHello("{broken", hello), L"malformed OBS JSON is rejected");

    int op = -1;
    Check(ParseObsOp(R"({"op":5,"d":{}})", op) && op == 5, L"OBS opcode parses");
    Check(!ParseObsOp(R"({"d":{}})", op), L"OBS message without opcode is rejected");
}

void TestObsCancellation()
{
    ObsSettings settings;
    settings.host = L"does-not-resolve.invalid";
    std::atomic<bool> cancel{true};
    std::wstring error;
    Check(!ObsClient::TestConnection(settings, error, &cancel), L"cancelled OBS test does not connect");
    Check(error.empty(), L"cancelled OBS test does not report a connection error");
}

void TestOverlayVisualState()
{
    StatusSnapshot status;
    status.obsConnState = ObsConnState::Disconnected;
    status.recorderState = RecorderState::Idle;
    status.lastError = L"OBS unavailable";
    status.recordingTime = std::chrono::seconds(1);

    const OverlayVisualState offline =
        MakeOverlayVisualState(status, Language::English, false);

    status.recordingTime = std::chrono::seconds(2);
    Check(
        !OverlayNeedsRender(
            offline,
            MakeOverlayVisualState(status, Language::English, false),
            false),
        L"overlay ignores recording time changes");

    status.obsConnState = ObsConnState::Connected;
    Check(
        OverlayNeedsRender(
            offline,
            MakeOverlayVisualState(status, Language::English, false),
            false),
        L"overlay detects OBS connection changes");

    status.obsConnState = ObsConnState::Disconnected;
    status.lastError.clear();
    Check(
        OverlayNeedsRender(
            offline,
            MakeOverlayVisualState(status, Language::English, false),
            false),
        L"overlay detects cleared errors");

    status.lastError = L"OBS unavailable";
    status.recorderState = RecorderState::Recording;
    const OverlayVisualState recording =
        MakeOverlayVisualState(status, Language::English, false);
    Check(
        OverlayNeedsRender(offline, recording, false),
        L"overlay detects recording start");

    status.recorderState = RecorderState::Idle;
    Check(
        OverlayNeedsRender(
            recording,
            MakeOverlayVisualState(status, Language::English, false),
            false),
        L"overlay detects recording stop");

    OverlayRefreshState refresh;
    const auto now = std::chrono::steady_clock::now();
    refresh.SetDesired(offline);
    Check(refresh.ShouldSubmit(now), L"new overlay state is submitted");
    refresh.MarkSubmitted(offline, now);
    Check(!refresh.ShouldSubmit(now), L"overlay waits for image load");

    refresh.SetDesired(recording);
    Check(!refresh.ShouldSubmit(now), L"new state waits behind in-flight image");
    refresh.MarkImageLoaded();
    Check(refresh.ShouldSubmit(now), L"newer state submits after image load");

    refresh.MarkSubmitted(recording, now);
    refresh.MarkImageFailed(now);
    Check(!refresh.ShouldSubmit(now), L"failed overlay image uses retry delay");
    Check(
        refresh.ShouldSubmit(now + std::chrono::milliseconds(300)),
        L"failed overlay image retries after delay");
}

void TestPresentationSettings()
{
    Check(
        OverlayRenderer::ButtonFontHeight == -44,
        L"overlay recording button uses the larger font");
    Check(
        OverlayRenderer::ButtonLetterSpacing == 1,
        L"overlay recording button uses compact letter spacing");

    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(
        OverlayRenderer::ButtonFontHeight,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI");
    Check(dc != nullptr && font != nullptr, L"overlay button font can be measured");
    if (dc && font) {
        const HGDIOBJ previous = SelectObject(dc, font);
        SetTextCharacterExtra(dc, OverlayRenderer::ButtonLetterSpacing);
        const std::wstring labels[] = {
            std::wstring(L"\u25CF  ") + Tr(Language::English, TextId::OverlayRec),
            std::wstring(L"\u25A0  ") + Tr(Language::English, TextId::OverlayStop),
            std::wstring(L"\u25CF  ") + Tr(Language::Russian, TextId::OverlayRec),
            std::wstring(L"\u25A0  ") + Tr(Language::Russian, TextId::OverlayStop),
        };
        for (const std::wstring& label : labels) {
            SIZE size{};
            const BOOL measured = GetTextExtentPoint32W(
                dc,
                label.c_str(),
                static_cast<int>(label.size()),
                &size);
            Check(measured != FALSE, L"overlay button label can be measured");
            Check(size.cx <= 420, L"overlay button label fits with horizontal padding");
        }
        SelectObject(dc, previous);
    }
    if (font) {
        DeleteObject(font);
    }
    if (dc) {
        DeleteDC(dc);
    }

    const std::string styles = ReadFile(
        ProjectRoot() / L"web" / L"styles.css");
    Check(
        styles.find("@keyframes page-enter") != std::string::npos,
        L"desktop pages have an enter animation");
    Check(
        styles.find("@keyframes reveal") != std::string::npos,
        L"desktop status elements have a reveal animation");
    Check(
        styles.find(".result:not(:empty)") != std::string::npos,
        L"desktop operation results animate when shown");
    Check(
        styles.find("prefers-reduced-motion: reduce") != std::string::npos,
        L"desktop animations respect reduced motion");
}

} // namespace

int wmain()
{
    Diagnostics log;
    const auto temp = std::filesystem::temp_directory_path() / L"VRecSmoke";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    TestSettingsRoundTrip(temp / L"settings.json", log);
    TestSettingsMigration(temp / L"legacy.json", log);
    TestFreshSettingsDoNotConnect(temp / L"fresh.json", log);
    TestMalformedSettings(temp / L"malformed.json", log);
    TestWebBridgeCodec();
    TestObsProtocol();
    TestObsCancellation();
    TestOverlayVisualState();
    TestPresentationSettings();

    if (failures != 0) {
        std::wcerr << failures << L" test(s) failed\n";
        return 1;
    }

    std::wcout << L"All VRec smoke tests passed\n";
    return 0;
}
