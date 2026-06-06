#include "WebBridgeCodec.h"

#include "TextUtil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string_view>

namespace {

using nlohmann::json;

template <typename T>
T ValueOr(const json& object, const char* key, T fallback)
{
    if (!object.is_object()) {
        return fallback;
    }

    const auto value = object.find(key);
    if (value == object.end()) {
        return fallback;
    }

    try {
        return value->get<T>();
    } catch (const json::exception&) {
        return fallback;
    }
}

const json& ObjectOrEmpty(const json& parent, const char* key)
{
    static const json empty = json::object();
    if (!parent.is_object()) {
        return empty;
    }

    const auto child = parent.find(key);
    return child != parent.end() && child->is_object() ? *child : empty;
}

Language ParseLanguage(const std::string& value, Language fallback)
{
    if (value == "ru") {
        return Language::Russian;
    }
    if (value == "en") {
        return Language::English;
    }
    return fallback;
}

Hand ParseHand(const std::string& value, Hand fallback)
{
    if (value == "left") {
        return Hand::Left;
    }
    if (value == "right") {
        return Hand::Right;
    }
    return fallback;
}

OverlayPlacement ParsePlacement(const std::string& value, OverlayPlacement fallback)
{
    if (value == "underController") {
        return OverlayPlacement::UnderController;
    }
    if (value == "wristOutside") {
        return OverlayPlacement::WristOutside;
    }
    return fallback;
}

const char* LanguageName(Language language)
{
    return language == Language::Russian ? "ru" : "en";
}

const char* HandName(Hand hand)
{
    return hand == Hand::Left ? "left" : "right";
}

const char* PlacementName(OverlayPlacement placement)
{
    return placement == OverlayPlacement::UnderController ? "underController" : "wristOutside";
}

const char* ObsStateName(const StatusSnapshot& status)
{
    switch (status.obsConnState) {
    case ObsConnState::Connected:
        return "connected";
    case ObsConnState::Connecting:
        return "connecting";
    default:
        return status.lastError.empty() ? "disconnected" : "error";
    }
}

std::wstring Encode(const json& value)
{
    return Utf8ToWide(value.dump());
}

} // namespace

bool ParseWebBridgeCommand(
    const std::wstring& message,
    const Settings& currentSettings,
    WebBridgeCommand& command,
    std::wstring& error)
{
    error.clear();

    try {
        const json root = json::parse(WideToUtf8(message));
        if (!root.is_object()) {
            error = L"Bridge message must be a JSON object";
            return false;
        }

        const std::string type = ValueOr(root, "type", std::string());
        if (type.empty()) {
            error = L"Bridge message is missing a type";
            return false;
        }

        const json& payload = ObjectOrEmpty(root, "payload");
        command.settings = currentSettings;
        command.obsSettings = currentSettings.obs;

        if (type == "getSettings") {
            command.type = WebBridgeCommandType::GetSettings;
        } else if (type == "applySettings") {
            command.type = WebBridgeCommandType::ApplySettings;
            command.settings.language = ParseLanguage(
                ValueOr(payload, "language", std::string(LanguageName(currentSettings.language))),
                currentSettings.language);

            const json& obs = ObjectOrEmpty(payload, "obs");
            command.settings.obs.host = Utf8ToWide(ValueOr(obs, "host", WideToUtf8(currentSettings.obs.host)));
            command.settings.obs.port = std::clamp(ValueOr(obs, "port", currentSettings.obs.port), 1, 65535);
            command.settings.obs.password = Utf8ToWide(ValueOr(obs, "password", WideToUtf8(currentSettings.obs.password)));
            if (command.settings.obs.host != currentSettings.obs.host ||
                command.settings.obs.port != currentSettings.obs.port ||
                command.settings.obs.password != currentSettings.obs.password) {
                command.settings.obsConfigured = false;
            }

            const json& overlay = ObjectOrEmpty(payload, "overlay");
            command.settings.overlay.hand = ParseHand(
                ValueOr(overlay, "hand", std::string(HandName(currentSettings.overlay.hand))),
                currentSettings.overlay.hand);
            command.settings.overlay.placement = ParsePlacement(
                ValueOr(overlay, "placement", std::string(PlacementName(currentSettings.overlay.placement))),
                currentSettings.overlay.placement);

            const json& advanced = ObjectOrEmpty(payload, "advanced");
            command.settings.advanced.logLevel = Utf8ToWide(
                ValueOr(advanced, "logLevel", WideToUtf8(currentSettings.advanced.logLevel)));
        } else if (type == "testConnection") {
            command.type = WebBridgeCommandType::TestConnection;
            command.obsSettings.host = Utf8ToWide(ValueOr(payload, "host", WideToUtf8(currentSettings.obs.host)));
            command.obsSettings.port = std::clamp(ValueOr(payload, "port", currentSettings.obs.port), 1, 65535);
            command.obsSettings.password = Utf8ToWide(ValueOr(payload, "password", WideToUtf8(currentSettings.obs.password)));
        } else if (type == "exportSupportReport") {
            command.type = WebBridgeCommandType::ExportSupportReport;
        } else if (type == "resetSettings") {
            command.type = WebBridgeCommandType::ResetSettings;
        } else if (type == "windowMinimize") {
            command.type = WebBridgeCommandType::WindowMinimize;
        } else if (type == "windowClose") {
            command.type = WebBridgeCommandType::WindowClose;
        } else if (type == "windowDragStart") {
            command.type = WebBridgeCommandType::WindowDragStart;
        } else if (type == "openRepo") {
            command.type = WebBridgeCommandType::OpenRepository;
        } else {
            error = L"Unknown bridge message type: " + Utf8ToWide(type);
            return false;
        }
        return true;
    } catch (const json::exception& exception) {
        error = L"Invalid bridge JSON: " + Utf8ToWide(exception.what());
        return false;
    }
}

std::wstring EncodeSettingsMessage(const Settings& settings)
{
    const json message = {
        { "type", "settings" },
        { "payload", {
            { "language", LanguageName(settings.language) },
            { "obs", {
                { "host", WideToUtf8(settings.obs.host) },
                { "port", settings.obs.port },
                { "password", WideToUtf8(settings.obs.password) },
            } },
            { "overlay", {
                { "hand", HandName(settings.overlay.hand) },
                { "placement", PlacementName(settings.overlay.placement) },
            } },
            { "advanced", {
                { "logLevel", WideToUtf8(settings.advanced.logLevel) },
            } },
        } },
    };
    return Encode(message);
}

std::wstring EncodeAboutMessage(const std::wstring& version, const std::wstring& buildType)
{
    return Encode({
        { "type", "about" },
        { "payload", {
            { "version", WideToUtf8(version) },
            { "build", WideToUtf8(buildType) },
        } },
    });
}

std::wstring EncodeStatusMessage(const StatusSnapshot& status)
{
    json log = json::array();
    for (const std::wstring& line : status.logLines) {
        log.push_back(WideToUtf8(line));
    }

    return Encode({
        { "type", "status" },
        { "payload", {
            { "obsConnState", ObsStateName(status) },
            { "lastError", WideToUtf8(status.lastError) },
            { "recorderState", status.recorderState == RecorderState::Recording ? "recording" : "idle" },
            { "recordingSeconds", status.recordingTime.count() },
            { "log", std::move(log) },
        } },
    });
}

std::wstring EncodeOperationResult(const char* type, bool ok, const std::wstring& error)
{
    return Encode({
        { "type", type },
        { "payload", {
            { "ok", ok },
            { "error", WideToUtf8(error) },
        } },
    });
}
