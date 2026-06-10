#include "SettingsStore.h"

#include "TextUtil.h"

#include <ShlObj.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace {

using nlohmann::json;

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

template <typename T>
T ValueOr(const json& object, const char* key, T fallback)
{
    const auto value = object.find(key);
    if (value == object.end() || !value->is_primitive()) {
        return fallback;
    }

    try {
        return value->get<T>();
    } catch (const json::exception&) {
        return fallback;
    }
}

Hand ParseHand(const std::string& value, Hand fallback)
{
    if (value == "left" || value == "Left") {
        return Hand::Left;
    }
    if (value == "right" || value == "Right") {
        return Hand::Right;
    }
    return fallback;
}

OverlayPlacement ParsePlacement(const std::string& value, OverlayPlacement fallback)
{
    if (value == "underController" || value == "under" || value == "Under controller") {
        return OverlayPlacement::UnderController;
    }
    if (value == "wristOutside" || value == "outside" || value == "Wrist outside") {
        return OverlayPlacement::WristOutside;
    }
    return fallback;
}

Language ParseLanguage(const std::string& value, Language fallback)
{
    if (value == "ru" || value == "ru_ru" || value == "Russian") {
        return Language::Russian;
    }
    if (value == "en" || value == "en_us" || value == "English") {
        return Language::English;
    }
    return fallback;
}

const char* HandName(Hand hand)
{
    return hand == Hand::Left ? "left" : "right";
}

const char* PlacementName(OverlayPlacement placement)
{
    return placement == OverlayPlacement::UnderController ? "underController" : "wristOutside";
}

const char* LanguageName(Language language)
{
    return language == Language::Russian ? "ru" : "en";
}

} // namespace

SettingsStore::SettingsStore(Diagnostics* diagnostics)
    : SettingsStore(KnownFolderPath(FOLDERID_RoamingAppData) / L"VRec" / L"settings.json", diagnostics)
{
}

SettingsStore::SettingsStore(std::filesystem::path path, Diagnostics* diagnostics)
    : path_(std::move(path))
    , diagnostics_(diagnostics)
{
}

Settings SettingsStore::Defaults()
{
    return {};
}

void SettingsStore::Normalize(Settings& settings)
{
    settings.version = 3;
    if (settings.obs.host.empty()) {
        settings.obs.host = L"127.0.0.1";
    }
    settings.obs.port = std::clamp(settings.obs.port, 1, 65535);
    if (settings.advanced.logLevel != L"debug" && settings.advanced.logLevel != L"info") {
        settings.advanced.logLevel = L"info";
    }
    settings.overlay.hideAngleDegrees = std::clamp(
        settings.overlay.hideAngleDegrees,
        kOverlayHideAngleMinDegrees,
        kOverlayHideAngleMaxDegrees);
    settings.overlay.offsetX = std::clamp(
        settings.overlay.offsetX,
        kPositionOffsetMinMeters,
        kPositionOffsetMaxMeters);
    settings.overlay.offsetY = std::clamp(
        settings.overlay.offsetY,
        kPositionOffsetMinMeters,
        kPositionOffsetMaxMeters);
    settings.overlay.offsetZ = std::clamp(
        settings.overlay.offsetZ,
        kPositionOffsetMinMeters,
        kPositionOffsetMaxMeters);
    settings.overlay.scale = std::clamp(
        settings.overlay.scale,
        kPositionScaleMin,
        kPositionScaleMax);
    settings.overlay.yawDegrees = std::clamp(
        settings.overlay.yawDegrees,
        kPositionYawMinDegrees,
        kPositionYawMaxDegrees);
}

Settings SettingsStore::Load()
{
    Settings settings = Defaults();

    if (!std::filesystem::exists(path_)) {
        Save(settings);
        return settings;
    }

    try {
        const json document = json::parse(ReadAll(path_));
        if (!document.is_object()) {
            throw json::type_error::create(302, "settings root must be an object", &document);
        }

        settings.language = ParseLanguage(ValueOr(document, "language", std::string(LanguageName(settings.language))), settings.language);
        settings.obs.host = Utf8ToWide(ValueOr(document, "obsHost", WideToUtf8(settings.obs.host)));
        settings.obs.port = ValueOr(document, "obsPort", settings.obs.port);
        settings.obs.password = Utf8ToWide(ValueOr(document, "obsPassword", WideToUtf8(settings.obs.password)));
        const auto configured = document.find("obsConfigured");
        if (configured != document.end() && configured->is_boolean()) {
            settings.obsConfigured = configured->get<bool>();
        } else {
            settings.obsConfigured =
                !settings.obs.password.empty() ||
                settings.obs.host != L"127.0.0.1" ||
                settings.obs.port != 4455;
        }
        settings.overlay.hand = ParseHand(ValueOr(document, "overlayHand", std::string(HandName(settings.overlay.hand))), settings.overlay.hand);
        settings.overlay.placement = ParsePlacement(
            ValueOr(document, "overlayPlacement", std::string(PlacementName(settings.overlay.placement))),
            settings.overlay.placement);
        settings.overlay.hideAngleDegrees = ValueOr(document, "hideAngleDegrees", settings.overlay.hideAngleDegrees);
        settings.overlay.offsetX = ValueOr(document, "positionOffsetX", settings.overlay.offsetX);
        settings.overlay.offsetY = ValueOr(document, "positionOffsetY", settings.overlay.offsetY);
        settings.overlay.offsetZ = ValueOr(document, "positionOffsetZ", settings.overlay.offsetZ);
        settings.overlay.scale = ValueOr(document, "positionScale", settings.overlay.scale);
        settings.overlay.yawDegrees = ValueOr(document, "positionYawDeg", settings.overlay.yawDegrees);
        settings.advanced.logLevel = Utf8ToWide(ValueOr(document, "logLevel", WideToUtf8(settings.advanced.logLevel)));
        settings.advanced.closeToTray = ValueOr(document, "closeToTray", settings.advanced.closeToTray);
    } catch (const json::exception&) {
        if (diagnostics_) {
            diagnostics_->LogWarning(L"Settings parse failed; using defaults");
        }
        settings = Defaults();
    } catch (const std::exception&) {
        if (diagnostics_) {
            diagnostics_->LogWarning(L"Settings could not be read; using defaults");
        }
        settings = Defaults();
    }

    Normalize(settings);
    return settings;
}

bool SettingsStore::Save(const Settings& settings)
{
    try {
        Settings normalized = settings;
        Normalize(normalized);

        std::filesystem::create_directories(path_.parent_path());
        const json document = {
            { "version", normalized.version },
            { "language", LanguageName(normalized.language) },
            { "obsHost", WideToUtf8(normalized.obs.host) },
            { "obsPort", normalized.obs.port },
            { "obsPassword", WideToUtf8(normalized.obs.password) },
            { "obsConfigured", normalized.obsConfigured },
            { "overlayHand", HandName(normalized.overlay.hand) },
            { "overlayPlacement", PlacementName(normalized.overlay.placement) },
            { "hideAngleDegrees", normalized.overlay.hideAngleDegrees },
            { "positionOffsetX", normalized.overlay.offsetX },
            { "positionOffsetY", normalized.overlay.offsetY },
            { "positionOffsetZ", normalized.overlay.offsetZ },
            { "positionScale", normalized.overlay.scale },
            { "positionYawDeg", normalized.overlay.yawDegrees },
            { "logLevel", WideToUtf8(normalized.advanced.logLevel) },
            { "closeToTray", normalized.advanced.closeToTray },
        };

        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        file << document.dump(2) << '\n';
        if (!file) {
            throw std::ios_base::failure("settings write failed");
        }
        return true;
    } catch (const std::exception&) {
        if (diagnostics_) {
            diagnostics_->LogError(L"Settings save failed");
        }
        return false;
    }
}
