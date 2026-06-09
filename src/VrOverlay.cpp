#include "VrOverlay.h"

#include "TextUtil.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <openvr.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>

namespace {

constexpr const char* kAppKey = "com.local.vrec";
constexpr const char* kOverlayKey = "com.local.vrec.hand";
constexpr const char* kCursorOverlayKey = "com.local.vrec.cursor";
constexpr const char* kOverlayName = "VRec";
constexpr const char* kCursorOverlayName = "VRec Pointer";
constexpr const char* kActionManifestFile = "actions.json";
constexpr const char* kActionSetPath = "/actions/vrec";
constexpr const char* kPointerPoseActionPath = "/actions/vrec/in/pointer_pose";
constexpr const char* kClickActionPath = "/actions/vrec/in/click";
constexpr const char* kHapticActionPath = "/actions/vrec/out/haptic";
constexpr const char* kLeftHandPath = "/user/hand/left";
constexpr const char* kRightHandPath = "/user/hand/right";
constexpr int kOverlayWidth = OverlayRenderer::Width;
constexpr int kOverlayHeight = OverlayRenderer::Height;
constexpr int kCursorOverlaySize = 64;
constexpr float kCursorOverlayWidthMeters = 0.014f;
constexpr float kCursorLiftMeters = 0.008f;
constexpr bool kDebugOverlayCheckerDefault = false;
constexpr double kPi = 3.14159265358979323846;
constexpr float kHideByAngleShowDot = 0.25881905f; // cos(75 deg)
constexpr float kHideByAngleHideDot = 0.17364818f; // cos(80 deg)
constexpr auto kMainOverlayFadeDuration = std::chrono::milliseconds(80);


using nlohmann::json;

constexpr const wchar_t* kSteamRegistryKey = L"Software\\Valve\\Steam";
constexpr const wchar_t* kSteamRegistryWow64Key = L"Software\\WOW6432Node\\Valve\\Steam";


RECT RecButtonRect()
{
    return RECT{ 22, 62, 490, 236 };
}

struct OverlayPose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double pitchDeg = 0.0;
    double yawDeg = 0.0;
    double rollDeg = 0.0;
};

struct PointerHit {
    bool hit = false;
    bool clickDown = false;
    bool clickChanged = false;
    bool steamVrInput = false;
    vr::VRInputValueHandle_t inputSource = vr::k_ulInvalidInputValueHandle;
    vr::TrackedDeviceIndex_t controller = vr::k_unTrackedDeviceIndexInvalid;
    vr::HmdVector3_t source{};
    vr::VROverlayIntersectionResults_t intersection{};
    float x = 0.0f;
    float yOpenVr = 0.0f;
    float yTopLeft = 0.0f;
};

double DegToRad(double value)
{
    return value * kPi / 180.0;
}

bool PtInRectPixels(const RECT& rect, float x, float y)
{
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

float OverlayMouseYToPixels(float y)
{
    return std::clamp(static_cast<float>(kOverlayHeight) - y, 0.0f, static_cast<float>(kOverlayHeight));
}

float OverlayMouseXToPixels(float x)
{
    return std::clamp(x, 0.0f, static_cast<float>(kOverlayWidth));
}

std::wstring OverlayErrorToString(vr::EVROverlayError error)
{
    if (error == vr::VROverlayError_None) {
        return L"None";
    }
    const char* name = vr::VROverlay() ? vr::VROverlay()->GetOverlayErrorNameFromEnum(error) : nullptr;
    std::stringstream stream;
    stream << (name ? name : "VROverlayError") << " (" << static_cast<int>(error) << ")";
    return Utf8ToWide(stream.str());
}

void LogOverlayError(Diagnostics* diagnostics, const wchar_t* action, vr::EVROverlayError error)
{
    if (!diagnostics || error == vr::VROverlayError_None) {
        return;
    }

    const std::wstring message = std::wstring(action) + L" failed: " + OverlayErrorToString(error);

    // dedup so a stuck overlay call doesn't spam the log every frame. linear scan, but
    // the set of distinct error strings is tiny so it never grows enough to matter.
    static std::mutex loggedMutex;
    static std::vector<std::wstring> loggedMessages;
    {
        std::scoped_lock lock(loggedMutex);
        if (std::find(loggedMessages.begin(), loggedMessages.end(), message) != loggedMessages.end()) {
            return;
        }
        loggedMessages.push_back(message);
    }

    diagnostics->LogWarning(message);
}

std::wstring InputErrorToString(vr::EVRInputError error)
{
    std::wstringstream stream;
    stream << L"EVRInputError(" << static_cast<int>(error) << L")";
    return stream.str();
}

bool DebugOverlayCheckerEnabled()
{
    wchar_t value[16]{};
    const DWORD count = GetEnvironmentVariableW(L"VREC_DEBUG_OVERLAY", value, ARRAYSIZE(value));
    if (count == 0) {
        return kDebugOverlayCheckerDefault;
    }
    return value[0] == L'1' || value[0] == L't' || value[0] == L'T' || value[0] == L'y' || value[0] == L'Y';
}

std::vector<uint8_t> BuildCursorRgba()
{
    std::vector<uint8_t> output(static_cast<size_t>(kCursorOverlaySize) * kCursorOverlaySize * 4, 0);
    const float center = (static_cast<float>(kCursorOverlaySize) - 1.0f) * 0.5f;
    constexpr float radius = 18.0f;
    constexpr float ringRadius = 24.0f;

    for (int y = 0; y < kCursorOverlaySize; ++y) {
        for (int x = 0; x < kCursorOverlaySize; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const size_t index = (static_cast<size_t>(y) * kCursorOverlaySize + x) * 4;
            if (distance <= radius) {
                output[index + 0] = 25;
                output[index + 1] = 216;
                output[index + 2] = 236;
                output[index + 3] = 235;
            } else if (distance <= ringRadius) {
                output[index + 0] = 238;
                output[index + 1] = 252;
                output[index + 2] = 255;
                output[index + 3] = 190;
            }
        }
    }

    return output;
}

bool IsValidOverlayHandle(uint64_t handle)
{
    return handle != 0 && handle != vr::k_ulOverlayHandleInvalid;
}


bool IsNamedProcessRunning(const wchar_t* executableName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (CompareStringOrdinal(entry.szExeFile, -1, executableName, -1, TRUE) == CSTR_EQUAL) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool IsSteamVrProcessRunning()
{
    return IsNamedProcessRunning(L"vrserver.exe") ||
        IsNamedProcessRunning(L"vrcompositor.exe") ||
        IsNamedProcessRunning(L"vrdashboard.exe");
}

std::filesystem::path ReadRegistryStringPath(HKEY root, const wchar_t* subkey, const wchar_t* valueName)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }

    DWORD type = 0;
    DWORD bytes = 0;
    const LONG queryResult = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (queryResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return {};
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG readResult = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &bytes);
    RegCloseKey(key);
    if (readResult != ERROR_SUCCESS) {
        return {};
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return std::filesystem::path(value);
}

std::filesystem::path SteamConfigDirectory()
{
    const std::filesystem::path fromUserRegistry = ReadRegistryStringPath(
        HKEY_CURRENT_USER,
        kSteamRegistryKey,
        L"SteamPath");
    if (!fromUserRegistry.empty()) {
        return fromUserRegistry / L"config";
    }

    const std::filesystem::path fromMachineRegistry = ReadRegistryStringPath(
        HKEY_LOCAL_MACHINE,
        kSteamRegistryWow64Key,
        L"InstallPath");
    if (!fromMachineRegistry.empty()) {
        return fromMachineRegistry / L"config";
    }

    wchar_t programFilesX86[MAX_PATH]{};
    const DWORD count = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, ARRAYSIZE(programFilesX86));
    if (count > 0 && count < ARRAYSIZE(programFilesX86)) {
        return std::filesystem::path(programFilesX86) / L"Steam" / L"config";
    }

    return {};
}

std::string ReadUtf8File(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool WriteUtf8File(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

bool ManifestFileDeclaresVRec(const std::filesystem::path& path)
{
    try {
        const std::string text = ReadUtf8File(path);
        if (text.empty()) {
            return false;
        }
        const json document = json::parse(text);
        const auto applications = document.find("applications");
        if (applications == document.end() || !applications->is_array()) {
            return false;
        }
        for (const json& application : *applications) {
            const auto appKey = application.find("app_key");
            if (appKey != application.end() && appKey->is_string() && appKey->get<std::string>() == kAppKey) {
                return true;
            }
        }
    } catch (const std::exception&) {
    }
    return false;
}

bool LooksLikeStaleVRecManifestPath(const std::filesystem::path& path)
{
    std::wstring text = path.wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text.find(L"vrec") != std::wstring::npos &&
        text.size() >= std::wstring(L"app.vrmanifest").size() &&
        text.rfind(L"app.vrmanifest") == text.size() - std::wstring(L"app.vrmanifest").size();
}

std::wstring ComparablePath(std::filesystem::path path)
{
    try {
        path = std::filesystem::absolute(path).lexically_normal();
    } catch (const std::exception&) {
        path = path.lexically_normal();
    }

    std::wstring text = path.wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    while (!text.empty() && text.back() == L'\\') {
        text.pop_back();
    }
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
{
    return ComparablePath(a) == ComparablePath(b);
}

bool EnsureVRecManifestInSteamAppConfig(Diagnostics* diagnostics)
{
    const std::filesystem::path currentManifest = ExecutableDirectory() / L"app.vrmanifest";
    if (!std::filesystem::exists(currentManifest)) {
        if (diagnostics) {
            diagnostics->LogWarning(L"SteamVR app manifest not found: " + currentManifest.wstring());
        }
        return false;
    }

    const std::filesystem::path steamConfig = SteamConfigDirectory();
    if (steamConfig.empty()) {
        if (diagnostics) {
            diagnostics->LogWarning(L"Steam config directory was not found; SteamVR app registration skipped");
        }
        return false;
    }

    const std::filesystem::path appConfig = steamConfig / L"appconfig.json";
    json document = json::object();
    if (std::filesystem::exists(appConfig)) {
        try {
            const std::string text = ReadUtf8File(appConfig);
            if (!text.empty()) {
                document = json::parse(text);
            }
        } catch (const std::exception&) {
            if (diagnostics) {
                diagnostics->LogWarning(L"SteamVR appconfig.json could not be parsed; app registration skipped");
            }
            return false;
        }
    }
    if (!document.is_object()) {
        document = json::object();
    }

    json manifestPaths = json::array();
    const auto existing = document.find("manifest_paths");
    if (existing != document.end() && existing->is_array()) {
        manifestPaths = *existing;
    }

    json updated = json::array();
    bool changed = false;
    bool currentPresent = false;

    for (const json& item : manifestPaths) {
        if (!item.is_string()) {
            changed = true;
            continue;
        }

        const std::filesystem::path candidate = Utf8ToWide(item.get<std::string>());
        if (SamePath(candidate, currentManifest)) {
            if (!currentPresent) {
                updated.push_back(WideToUtf8(currentManifest.wstring()));
                currentPresent = true;
            } else {
                changed = true;
            }
            continue;
        }

        const bool staleVRec =
            (std::filesystem::exists(candidate) && ManifestFileDeclaresVRec(candidate)) ||
            (!std::filesystem::exists(candidate) && LooksLikeStaleVRecManifestPath(candidate));
        if (staleVRec) {
            changed = true;
            continue;
        }

        updated.push_back(item);
    }

    if (!currentPresent) {
        updated.push_back(WideToUtf8(currentManifest.wstring()));
        changed = true;
    }

    document["manifest_paths"] = std::move(updated);
    if (!changed) {
        return true;
    }

    const std::filesystem::path backup = appConfig.wstring() + L".vrec.bak";
    try {
        if (std::filesystem::exists(appConfig)) {
            std::filesystem::copy_file(appConfig, backup, std::filesystem::copy_options::overwrite_existing);
        }
    } catch (const std::exception&) {
    }

    const bool saved = WriteUtf8File(appConfig, document.dump(3) + "\n");
    if (diagnostics) {
        if (saved) {
            diagnostics->LogInfo(L"SteamVR app manifest path registered: " + currentManifest.wstring());
        } else {
            diagnostics->LogWarning(L"SteamVR appconfig.json could not be written: " + appConfig.wstring());
        }
    }
    return saved;
}

std::wstring ApplicationPropertyString(
    vr::IVRApplications* apps,
    const char* appKey,
    vr::EVRApplicationProperty property)
{
    if (!apps) {
        return {};
    }

    vr::EVRApplicationError error = vr::VRApplicationError_None;
    char stackBuffer[512]{};
    const uint32_t required = apps->GetApplicationPropertyString(
        appKey,
        property,
        stackBuffer,
        static_cast<uint32_t>(sizeof(stackBuffer)),
        &error);
    if (error != vr::VRApplicationError_None) {
        return {};
    }
    if (required <= sizeof(stackBuffer)) {
        return Utf8ToWide(stackBuffer);
    }

    std::vector<char> buffer(required + 1, '\0');
    error = vr::VRApplicationError_None;
    apps->GetApplicationPropertyString(
        appKey,
        property,
        buffer.data(),
        static_cast<uint32_t>(buffer.size()),
        &error);
    return error == vr::VRApplicationError_None ? Utf8ToWide(buffer.data()) : std::wstring();
}

bool RegisteredApplicationMatchesCurrentPath(vr::IVRApplications* apps, const std::filesystem::path& currentExecutableDir)
{
    const std::wstring registeredWorkingDir = ApplicationPropertyString(
        apps,
        kAppKey,
        vr::VRApplicationProperty_WorkingDirectory_String);
    if (!registeredWorkingDir.empty() &&
        SamePath(std::filesystem::path(registeredWorkingDir), currentExecutableDir)) {
        return true;
    }

    const std::wstring binaryPath = ApplicationPropertyString(
        apps,
        kAppKey,
        vr::VRApplicationProperty_BinaryPath_String);
    if (!binaryPath.empty()) {
        const std::filesystem::path binary = std::filesystem::path(binaryPath);
        if (binary.is_absolute()) {
            return SamePath(binary.parent_path(), currentExecutableDir);
        }
    }

    return false;
}

void AddUniquePath(std::vector<std::filesystem::path>& paths, std::filesystem::path path)
{
    if (path.empty()) {
        return;
    }

    const std::wstring comparable = ComparablePath(path);
    const auto found = std::find_if(paths.begin(), paths.end(), [&](const std::filesystem::path& existing) {
        return ComparablePath(existing) == comparable;
    });
    if (found == paths.end()) {
        paths.push_back(std::move(path));
    }
}

void RemoveRegisteredApplicationManifest(vr::IVRApplications* apps, const std::filesystem::path& currentManifest)
{
    if (!apps) {
        return;
    }

    std::vector<std::filesystem::path> manifests;
    AddUniquePath(manifests, currentManifest);

    const std::wstring registeredWorkingDir = ApplicationPropertyString(
        apps,
        kAppKey,
        vr::VRApplicationProperty_WorkingDirectory_String);
    if (!registeredWorkingDir.empty()) {
        AddUniquePath(manifests, std::filesystem::path(registeredWorkingDir) / L"app.vrmanifest");
    }

    const std::wstring binaryPath = ApplicationPropertyString(
        apps,
        kAppKey,
        vr::VRApplicationProperty_BinaryPath_String);
    if (!binaryPath.empty()) {
        std::filesystem::path binary(binaryPath);
        if (binary.is_absolute()) {
            AddUniquePath(manifests, binary.parent_path() / L"app.vrmanifest");
        }
    }

    for (const auto& manifest : manifests) {
        apps->RemoveApplicationManifest(WideToUtf8(manifest.wstring()).c_str());
    }
}

std::wstring ApplicationErrorToString(vr::IVRApplications* apps, vr::EVRApplicationError error)
{
    const char* name = apps ? apps->GetApplicationsErrorNameFromEnum(error) : nullptr;
    std::stringstream stream;
    stream << (name ? name : "EVRApplicationError") << " (" << static_cast<int>(error) << ")";
    return Utf8ToWide(stream.str());
}


vr::EVROverlayError CreateOrFindOverlay(const char* key, const char* name, vr::VROverlayHandle_t* handle)
{
    vr::EVROverlayError err = vr::VROverlay()->CreateOverlay(key, name, handle);
    if (err == vr::VROverlayError_KeyInUse) {
        err = vr::VROverlay()->FindOverlay(key, handle);
    }
    return err;
}

float VectorLength(const vr::HmdVector3_t& value)
{
    return std::sqrt(value.v[0] * value.v[0] + value.v[1] * value.v[1] + value.v[2] * value.v[2]);
}

vr::HmdVector3_t NormalizeVector(vr::HmdVector3_t value)
{
    const float length = VectorLength(value);
    if (length <= 0.00001f) {
        return vr::HmdVector3_t{ 0.0f, 0.0f, -1.0f };
    }

    value.v[0] /= length;
    value.v[1] /= length;
    value.v[2] /= length;
    return value;
}

vr::HmdVector3_t PoseTranslation(const vr::HmdMatrix34_t& pose)
{
    return vr::HmdVector3_t{ pose.m[0][3], pose.m[1][3], pose.m[2][3] };
}

float Dot(const vr::HmdVector3_t& a, const vr::HmdVector3_t& b)
{
    return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2];
}

vr::HmdVector3_t Subtract(const vr::HmdVector3_t& a, const vr::HmdVector3_t& b)
{
    return vr::HmdVector3_t{ a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2] };
}

vr::HmdVector3_t PoseForward(const vr::HmdMatrix34_t& pose)
{
    return NormalizeVector(vr::HmdVector3_t{
        -pose.m[0][2],
        -pose.m[1][2],
        -pose.m[2][2],
    });
}

vr::HmdVector3_t CursorLiftDirection(const vr::HmdMatrix34_t& transform, const vr::HmdVector3_t& pointerSource)
{
    vr::HmdVector3_t normal = NormalizeVector(vr::HmdVector3_t{
        -transform.m[0][2],
        -transform.m[1][2],
        -transform.m[2][2],
    });
    const vr::HmdVector3_t toSource = NormalizeVector(Subtract(pointerSource, PoseTranslation(transform)));
    if (Dot(normal, toSource) < 0.0f) {
        normal.v[0] = -normal.v[0];
        normal.v[1] = -normal.v[1];
        normal.v[2] = -normal.v[2];
    }
    return normal;
}

bool ComputeOverlayHit(vr::VROverlayHandle_t overlayHandle, const vr::HmdVector3_t& source, const vr::HmdVector3_t& direction, vr::VROverlayIntersectionResults_t* results)
{
    vr::VROverlayIntersectionParams_t params{};
    params.vSource = source;
    params.vDirection = direction;
    params.eOrigin = vr::TrackingUniverseStanding;
    return vr::VROverlay()->ComputeOverlayIntersection(overlayHandle, &params, results);
}

void FillPointerHitCoordinates(PointerHit& hit)
{
    hit.x = OverlayMouseXToPixels(hit.intersection.vUVs.v[0] * static_cast<float>(kOverlayWidth));
    hit.yOpenVr = std::clamp(hit.intersection.vUVs.v[1] * static_cast<float>(kOverlayHeight), 0.0f, static_cast<float>(kOverlayHeight));
    hit.yTopLeft = OverlayMouseYToPixels(hit.yOpenVr);
}

bool IsBetterPointerHit(const PointerHit& candidate, const PointerHit& current)
{
    if (!candidate.hit) {
        return false;
    }
    if (!current.hit) {
        return true;
    }
    if (candidate.clickDown != current.clickDown) {
        return candidate.clickDown;
    }
    if (candidate.clickChanged != current.clickChanged) {
        return candidate.clickChanged;
    }
    return candidate.intersection.fDistance < current.intersection.fDistance;
}

bool ComputeStrictOverlayHit(vr::VROverlayHandle_t overlayHandle, const vr::HmdMatrix34_t& pose, PointerHit& hit)
{
    const vr::HmdVector3_t source = PoseTranslation(pose);
    vr::VROverlayIntersectionResults_t result{};
    if (!ComputeOverlayHit(overlayHandle, source, PoseForward(pose), &result)) {
        return false;
    }

    hit.hit = true;
    hit.source = source;
    hit.intersection = result;
    FillPointerHitCoordinates(hit);
    return true;
}

// These poses were tuned in-headset relative to the controller grip. The left-hand
// pose mirrors the horizontal offset and orientation.
OverlayPose PresetPose(const Settings& settings)
{
    const double side = settings.overlay.hand == Hand::Right ? 1.0 : -1.0;
    OverlayPose pose;

    switch (settings.overlay.placement) {
    case OverlayPlacement::UnderController:
        pose.x = 0.0;
        pose.y = -0.030;
        pose.z = 0.205;
        pose.pitchDeg = 28.0;
        pose.yawDeg = 0.0;
        pose.rollDeg = 0.0;
        break;
    case OverlayPlacement::WristOutside:
    default:
        pose.x = side * 0.080;
        pose.y = -0.030;
        pose.z = 0.070;
        pose.pitchDeg = 0.0;
        pose.yawDeg = side * 118.0;
        pose.rollDeg = side * 88.0;
        break;
    }

    return pose;
}

vr::HmdMatrix34_t MatrixFromPose(const OverlayPose& pose)
{
    const double pitch = DegToRad(pose.pitchDeg);
    const double yaw = DegToRad(pose.yawDeg);
    const double roll = DegToRad(pose.rollDeg);

    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);

    vr::HmdMatrix34_t transform{};
    transform.m[0][0] = static_cast<float>(cy * cr + sy * sp * sr);
    transform.m[0][1] = static_cast<float>(sr * cp);
    transform.m[0][2] = static_cast<float>(-sy * cr + cy * sp * sr);

    transform.m[1][0] = static_cast<float>(-cy * sr + sy * sp * cr);
    transform.m[1][1] = static_cast<float>(cr * cp);
    transform.m[1][2] = static_cast<float>(sr * sy + cy * sp * cr);

    transform.m[2][0] = static_cast<float>(sy * cp);
    transform.m[2][1] = static_cast<float>(-sp);
    transform.m[2][2] = static_cast<float>(cy * cp);

    transform.m[0][3] = static_cast<float>(pose.x);
    transform.m[1][3] = static_cast<float>(pose.y);
    transform.m[2][3] = static_cast<float>(pose.z);
    return transform;
}

vr::HmdMatrix34_t MultiplyTransforms(const vr::HmdMatrix34_t& parent, const vr::HmdMatrix34_t& child)
{
    vr::HmdMatrix34_t out{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.m[row][col] =
                parent.m[row][0] * child.m[0][col] +
                parent.m[row][1] * child.m[1][col] +
                parent.m[row][2] * child.m[2][col];
        }
        out.m[row][3] =
            parent.m[row][0] * child.m[0][3] +
            parent.m[row][1] * child.m[1][3] +
            parent.m[row][2] * child.m[2][3] +
            parent.m[row][3];
    }
    return out;
}

vr::HmdVector3_t OverlayFaceNormal(const vr::HmdMatrix34_t& overlayToWorld)
{
    return NormalizeVector(vr::HmdVector3_t{
        overlayToWorld.m[0][2],
        overlayToWorld.m[1][2],
        overlayToWorld.m[2][2],
    });
}

} // namespace

VrOverlay::~VrOverlay()
{
    Stop();
}

bool VrOverlay::Start(
    const Settings& settings,
    Diagnostics* diagnostics,
    RecordingCommandCallback recordingCommand,
    StatusProvider statusProvider)
{
    {
        std::scoped_lock lock(mutex_);
        settings_ = settings;
        diagnostics_ = diagnostics;
        recordingCommand_ = std::move(recordingCommand);
        statusProvider_ = std::move(statusProvider);
    }

    EnsureVRecManifestInSteamAppConfig(diagnostics);

    stop_ = false;
    worker_ = std::thread(&VrOverlay::Run, this);
    return true;
}

void VrOverlay::Stop()
{
    stop_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void VrOverlay::UpdateSettings(const Settings& settings)
{
    std::scoped_lock lock(mutex_);
    const bool pointerSettingsChanged =
        settings_.overlay.hand != settings.overlay.hand ||
        settings_.overlay.placement != settings.overlay.placement;

    settings_ = settings;
    attachDirty_ = true;
    lastLegacyLeftTrigger_ = false;
    lastLegacyRightTrigger_ = false;

    if (pointerSettingsChanged) {
        pointerPolicyLogged_ = false;
        steamVrStrictRayLogged_ = false;
        legacyStrictRayLogged_ = false;
        cursorTransformFailureLogged_ = false;
    }
}

std::wstring VrOverlay::LastError() const
{
    std::scoped_lock lock(mutex_);
    return lastError_;
}

Settings VrOverlay::SettingsSnapshot() const
{
    std::scoped_lock lock(mutex_);
    return settings_;
}

void VrOverlay::Run()
{
    while (!stop_) {
        if (!overlayReady_) {
            if (reconnectBlockedUntilSteamVrExit_) {
                if (!IsSteamVrProcessRunning()) {
                    reconnectBlockedUntilSteamVrExit_ = false;
                    reconnectBlockLogged_ = false;
                } else {
                    if (diagnostics_ && !reconnectBlockLogged_) {
                        diagnostics_->LogDebug(L"Waiting for SteamVR shutdown before reconnecting");
                        reconnectBlockLogged_ = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
            }

            Initialize();
        }

        if (overlayReady_) {
            Tick();
        }

        if (overlayReady_ && vr::VROverlay()) {
            const vr::EVROverlayError syncError = vr::VROverlay()->WaitFrameSync(20);
            if (syncError != vr::VROverlayError_None) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    }

    ShutdownOpenVr();
}

bool VrOverlay::Initialize()
{
    if (!vr::VR_IsRuntimeInstalled()) {
        SetError(L"SteamVR runtime is not installed");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return false;
    }
    if (!IsSteamVrProcessRunning()) {
        SetError(L"SteamVR is not running");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return false;
    }

    vr::EVRInitError initError = vr::VRInitError_None;
    vr::VR_Init(&initError, vr::VRApplication_Overlay);
    if (initError != vr::VRInitError_None) {
        const char* message = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
        SetError(Utf8ToWide(message ? message : "VR_Init failed"));
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return false;
    }

    steamVrReady_ = true;
    headsetReady_ = vr::VRSystem() && vr::VRSystem()->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd);

    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError err = CreateOrFindOverlay(kOverlayKey, kOverlayName, &handle);
    if (err != vr::VROverlayError_None) {
        std::stringstream stream;
        stream << "CreateOverlay failed: " << err;
        SetError(Utf8ToWide(stream.str()));
        ShutdownOpenVr();
        return false;
    }

    overlayHandle_ = handle;
    LogOverlayError(
        diagnostics_,
        L"SetOverlayRenderingPid",
        vr::VROverlay()->SetOverlayRenderingPid(
            handle,
            GetCurrentProcessId()));
    LogOverlayError(diagnostics_, L"SetOverlayWidthInMeters", vr::VROverlay()->SetOverlayWidthInMeters(handle, 0.18f));
    LogOverlayError(diagnostics_, L"SetOverlaySortOrder", vr::VROverlay()->SetOverlaySortOrder(handle, 0));
    LogOverlayError(diagnostics_, L"SetOverlayAlpha", vr::VROverlay()->SetOverlayAlpha(handle, 1.0f));
    LogOverlayError(diagnostics_, L"SetOverlayFlag IgnoreTextureAlpha", vr::VROverlay()->SetOverlayFlag(handle, vr::VROverlayFlags_IgnoreTextureAlpha, false));
    LogOverlayError(diagnostics_, L"SetOverlayFlag MakeOverlaysInteractiveIfVisible", vr::VROverlay()->SetOverlayFlag(handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false));
    LogOverlayError(diagnostics_, L"SetOverlayFlag HideLaserIntersection", vr::VROverlay()->SetOverlayFlag(handle, vr::VROverlayFlags_HideLaserIntersection, true));
    LogOverlayError(diagnostics_, L"SetOverlayInputMethod", vr::VROverlay()->SetOverlayInputMethod(handle, vr::VROverlayInputMethod_None));

    vr::HmdVector2_t mouseScale{ static_cast<float>(kOverlayWidth), static_cast<float>(kOverlayHeight) };
    LogOverlayError(diagnostics_, L"SetOverlayMouseScale", vr::VROverlay()->SetOverlayMouseScale(handle, &mouseScale));
    LogOverlayError(diagnostics_, L"SetOverlayAlpha initial", vr::VROverlay()->SetOverlayAlpha(handle, mainOverlayAlpha_));
    const vr::EVROverlayError showError = vr::VROverlay()->ShowOverlay(handle);
    LogOverlayError(diagnostics_, L"ShowOverlay", showError);
    mainOverlayVisible_ = showError == vr::VROverlayError_None;
    hideByAngleVisible_ = mainOverlayVisible_;

    vr::VROverlayHandle_t cursorHandle = vr::k_ulOverlayHandleInvalid;
    const vr::EVROverlayError cursorCreateError = CreateOrFindOverlay(kCursorOverlayKey, kCursorOverlayName, &cursorHandle);
    if (cursorCreateError == vr::VROverlayError_None) {
        cursorOverlayHandle_ = cursorHandle;
        LogOverlayError(
            diagnostics_,
            L"SetCursorOverlayRenderingPid",
            vr::VROverlay()->SetOverlayRenderingPid(
                cursorHandle,
                GetCurrentProcessId()));
        LogOverlayError(diagnostics_, L"SetCursorOverlayWidthInMeters", vr::VROverlay()->SetOverlayWidthInMeters(cursorHandle, kCursorOverlayWidthMeters));
        LogOverlayError(diagnostics_, L"SetCursorOverlaySortOrder", vr::VROverlay()->SetOverlaySortOrder(cursorHandle, 100));
        LogOverlayError(diagnostics_, L"SetCursorOverlayAlpha", vr::VROverlay()->SetOverlayAlpha(cursorHandle, 1.0f));
        LogOverlayError(diagnostics_, L"SetCursorOverlayFlag IgnoreTextureAlpha", vr::VROverlay()->SetOverlayFlag(cursorHandle, vr::VROverlayFlags_IgnoreTextureAlpha, false));
        LogOverlayError(diagnostics_, L"SetCursorOverlayInputMethod", vr::VROverlay()->SetOverlayInputMethod(cursorHandle, vr::VROverlayInputMethod_None));
        std::vector<uint8_t> cursorBuffer = BuildCursorRgba();
        LogOverlayError(diagnostics_, L"SetCursorOverlayRaw", vr::VROverlay()->SetOverlayRaw(cursorHandle, cursorBuffer.data(), kCursorOverlaySize, kCursorOverlaySize, 4));
        vr::VROverlay()->HideOverlay(cursorHandle);
    } else {
        LogOverlayError(diagnostics_, L"CreateCursorOverlay", cursorCreateError);
    }

    inputReady_ = InitializeInput();

    overlayReady_ = true;
    {
        std::scoped_lock lock(mutex_);
        attachDirty_ = true;
        refreshState_.Reset();
        overlayImageFailureLogged_ = false;
    }
    manifestApplied_ = false;
    manifestErrorLogged_ = false;
    reconnectBlockedUntilSteamVrExit_ = false;
    reconnectBlockLogged_ = false;
    if (diagnostics_) {
        diagnostics_->LogInfo(L"SteamVR overlay initialized");
    }

    ApplyManifest();
    const StatusSnapshot status = statusProvider_ ? statusProvider_() : StatusSnapshot{};
    RenderOverlay(status);
    return true;
}

void VrOverlay::ShutdownOpenVr()
{
    if (IsValidOverlayHandle(cursorOverlayHandle_) && vr::VROverlay()) {
        vr::VROverlay()->HideOverlay(cursorOverlayHandle_);
        vr::VROverlay()->DestroyOverlay(cursorOverlayHandle_);
    }

    if (overlayHandle_ != 0 && overlayHandle_ != vr::k_ulOverlayHandleInvalid && vr::VROverlay()) {
        if (mainOverlayVisible_) {
            vr::VROverlay()->HideOverlay(overlayHandle_);
        }
        vr::VROverlay()->DestroyOverlay(overlayHandle_);
    }

    renderer_.Reset();
    {
        std::scoped_lock lock(mutex_);
        refreshState_.Reset();
        overlayImageFailureLogged_ = false;
    }

    cursorOverlayHandle_ = 0;
    cursorOverlayVisible_ = false;
    inputReady_ = false;
    actionSetHandle_ = 0;
    pointerPoseAction_ = 0;
    clickAction_ = 0;
    hapticAction_ = 0;
    leftHandSource_ = 0;
    rightHandSource_ = 0;
    poseInactiveLogged_ = false;
    clickInactiveLogged_ = false;
    pointerPolicyLogged_ = false;
    cursorTransformFailureLogged_ = false;
    steamVrStrictRayLogged_ = false;
    legacyStrictRayLogged_ = false;
    legacyFallbackReasonLogged_ = false;
    overlayHandle_ = 0;
    mainOverlayVisible_ = false;
    manifestApplied_ = false;
    manifestErrorLogged_ = false;
    hideByAngleVisible_ = false;
    mainOverlayFadeActive_ = false;
    mainOverlayFadeHideWhenDone_ = false;
    mainOverlayAlpha_ = 1.0f;
    mainOverlayFadeStartAlpha_ = 1.0f;
    mainOverlayTargetAlpha_ = 1.0f;
    overlayReady_ = false;
    controllerReady_ = false;

    if (steamVrReady_) {
        vr::VR_Shutdown();
    }
    steamVrReady_ = false;
    headsetReady_ = false;
}

void VrOverlay::Tick()
{
    if (!IsSteamVrProcessRunning()) {
        ShutdownOpenVr();
        SetError(L"SteamVR is not running");
        return;
    }
    if (!vr::VRSystem() || !vr::VROverlay()) {
        reconnectBlockedUntilSteamVrExit_ = true;
        ShutdownOpenVr();
        return;
    }
    if (PollSystemEvents()) {
        return;
    }

    headsetReady_ = vr::VRSystem()->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd);
    ApplyManifest();
    AttachToHand();
    UpdateMainOverlayVisibility();
    UpdateMainOverlayFade();
    UpdateControllerInteraction();
    PollEvents();
    const StatusSnapshot status = statusProvider_ ? statusProvider_() : StatusSnapshot{};
    const Settings settings = SettingsSnapshot();
    const OverlayVisualState visualState = MakeOverlayVisualState(
        status,
        settings.language,
        DebugOverlayCheckerEnabled());
    bool shouldRender = false;
    {
        std::scoped_lock lock(mutex_);
        refreshState_.SetDesired(visualState);
        shouldRender = refreshState_.ShouldSubmit(
            std::chrono::steady_clock::now());
    }
    if (shouldRender) {
        RenderOverlay(status);
    }
}

void VrOverlay::AttachToHand()
{
    const Settings settings = SettingsSnapshot();
    const vr::ETrackedControllerRole role = settings.overlay.hand == Hand::Left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    const vr::TrackedDeviceIndex_t controller = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(role);
    if (controller == vr::k_unTrackedDeviceIndexInvalid || !vr::VRSystem()->IsTrackedDeviceConnected(controller)) {
        controllerReady_ = false;
        return;
    }

    controllerReady_ = true;

    {
        std::scoped_lock lock(mutex_);
        if (!attachDirty_ &&
            lastAttachedController_ == controller &&
            lastAttachedHand_ == settings.overlay.hand &&
            lastAttachedPlacement_ == settings.overlay.placement) {
            return;
        }
    }

    const OverlayPose pose = PresetPose(settings);
    const vr::HmdMatrix34_t transform = MatrixFromPose(pose);

    LogOverlayError(diagnostics_, L"SetOverlayTransformTrackedDeviceRelative", vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(overlayHandle_, controller, &transform));
    LogOverlayError(diagnostics_, L"SetOverlayWidthInMeters", vr::VROverlay()->SetOverlayWidthInMeters(overlayHandle_, 0.18f));

    {
        std::scoped_lock lock(mutex_);
        attachDirty_ = false;
        lastAttachedController_ = controller;
        lastAttachedHand_ = settings.overlay.hand;
        lastAttachedPlacement_ = settings.overlay.placement;
    }
}

void VrOverlay::UpdateMainOverlayVisibility()
{
    if (!vr::VRSystem() || !vr::VROverlay() || !IsValidOverlayHandle(overlayHandle_)) {
        return;
    }

    const Settings settings = SettingsSnapshot();
    const vr::ETrackedControllerRole role = settings.overlay.hand == Hand::Left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    const vr::TrackedDeviceIndex_t controller = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(role);
    if (controller == vr::k_unTrackedDeviceIndexInvalid) {
        return;
    }

    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);
    const vr::TrackedDevicePose_t& hmdPose = poses[vr::k_unTrackedDeviceIndex_Hmd];
    const vr::TrackedDevicePose_t& controllerPose = poses[controller];
    if (!hmdPose.bPoseIsValid || !controllerPose.bPoseIsValid) {
        return;
    }

    const vr::HmdMatrix34_t relativeOverlay = MatrixFromPose(PresetPose(settings));
    const vr::HmdMatrix34_t overlayWorld = MultiplyTransforms(controllerPose.mDeviceToAbsoluteTracking, relativeOverlay);
    const vr::HmdVector3_t overlayPosition = PoseTranslation(overlayWorld);
    const vr::HmdVector3_t hmdPosition = PoseTranslation(hmdPose.mDeviceToAbsoluteTracking);
    const vr::HmdVector3_t toHmd = NormalizeVector(Subtract(hmdPosition, overlayPosition));
    const float dot = Dot(OverlayFaceNormal(overlayWorld), toHmd);

    bool shouldBeVisible = hideByAngleVisible_;
    if (hideByAngleVisible_) {
        if (dot < kHideByAngleHideDot) {
            shouldBeVisible = false;
        }
    } else if (dot > kHideByAngleShowDot) {
        shouldBeVisible = true;
    }

    if (shouldBeVisible == hideByAngleVisible_) {
        return;
    }

    hideByAngleVisible_ = shouldBeVisible;
    BeginMainOverlayFade(shouldBeVisible);
}

void VrOverlay::BeginMainOverlayFade(bool visible)
{
    if (!vr::VROverlay() || !IsValidOverlayHandle(overlayHandle_)) {
        return;
    }

    if (visible) {
        if (!mainOverlayVisible_) {
            LogOverlayError(diagnostics_, L"SetOverlayAlpha fade show start", vr::VROverlay()->SetOverlayAlpha(overlayHandle_, 0.0f));
            mainOverlayAlpha_ = 0.0f;
            const vr::EVROverlayError error = vr::VROverlay()->ShowOverlay(overlayHandle_);
            LogOverlayError(diagnostics_, L"ShowOverlay hide-by-angle", error);
            if (error != vr::VROverlayError_None) {
                hideByAngleVisible_ = false;
                return;
            }
            mainOverlayVisible_ = true;
        }
        mainOverlayFadeHideWhenDone_ = false;
        mainOverlayFadeStartAlpha_ = mainOverlayAlpha_;
        mainOverlayTargetAlpha_ = 1.0f;
        mainOverlayFadeStart_ = std::chrono::steady_clock::now();
        mainOverlayFadeActive_ = mainOverlayAlpha_ < mainOverlayTargetAlpha_;
    } else {
        HideCursorOverlay();
        mainOverlayFadeStartAlpha_ = mainOverlayAlpha_;
        mainOverlayTargetAlpha_ = 0.0f;
        mainOverlayFadeHideWhenDone_ = true;
        mainOverlayFadeStart_ = std::chrono::steady_clock::now();
        mainOverlayFadeActive_ = mainOverlayAlpha_ > mainOverlayTargetAlpha_;
        if (!mainOverlayFadeActive_) {
            const vr::EVROverlayError error = vr::VROverlay()->HideOverlay(overlayHandle_);
            LogOverlayError(diagnostics_, L"HideOverlay hide-by-angle", error);
            if (error == vr::VROverlayError_None) {
                mainOverlayVisible_ = false;
            }
        }
    }
}

void VrOverlay::UpdateMainOverlayFade()
{
    if (!mainOverlayFadeActive_ || !vr::VROverlay() || !IsValidOverlayHandle(overlayHandle_)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - mainOverlayFadeStart_;
    const float t = std::clamp(
        static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) /
            static_cast<float>(kMainOverlayFadeDuration.count()),
        0.0f,
        1.0f);
    mainOverlayAlpha_ = mainOverlayFadeStartAlpha_ + (mainOverlayTargetAlpha_ - mainOverlayFadeStartAlpha_) * t;
    LogOverlayError(diagnostics_, L"SetOverlayAlpha fade", vr::VROverlay()->SetOverlayAlpha(overlayHandle_, mainOverlayAlpha_));

    if (t < 1.0f) {
        return;
    }

    mainOverlayAlpha_ = mainOverlayTargetAlpha_;
    mainOverlayFadeActive_ = false;
    LogOverlayError(diagnostics_, L"SetOverlayAlpha fade final", vr::VROverlay()->SetOverlayAlpha(overlayHandle_, mainOverlayAlpha_));
    if (mainOverlayFadeHideWhenDone_) {
        const vr::EVROverlayError error = vr::VROverlay()->HideOverlay(overlayHandle_);
        LogOverlayError(diagnostics_, L"HideOverlay hide-by-angle", error);
        if (error == vr::VROverlayError_None) {
            mainOverlayVisible_ = false;
        }
        mainOverlayFadeHideWhenDone_ = false;
    }
}

bool VrOverlay::PollSystemEvents()
{
    if (!vr::VRSystem()) {
        reconnectBlockedUntilSteamVrExit_ = true;
        ShutdownOpenVr();
        return true;
    }

    bool shutdownRequested = false;
    vr::VREvent_t event{};
    while (vr::VRSystem()->PollNextEvent(&event, sizeof(event))) {
        if (event.eventType == vr::VREvent_Quit ||
            event.eventType == vr::VREvent_DriverRequestedQuit) {
            shutdownRequested = true;
        }
    }

    if (!shutdownRequested) {
        return false;
    }

    reconnectBlockedUntilSteamVrExit_ = true;
    reconnectBlockLogged_ = false;
    SetError(L"SteamVR is shutting down");
    if (diagnostics_) {
        diagnostics_->LogInfo(L"SteamVR shutdown requested; overlay disconnected");
    }
    ShutdownOpenVr();
    return true;
}

void VrOverlay::PollEvents()
{
    vr::VREvent_t event{};
    while (vr::VROverlay()->PollNextOverlayEvent(overlayHandle_, &event, sizeof(event))) {
        if (event.eventType == vr::VREvent_ImageLoaded) {
            {
                std::scoped_lock lock(mutex_);
                refreshState_.MarkImageLoaded();
                overlayImageFailureLogged_ = false;
            }
            if (diagnostics_) {
                diagnostics_->LogDebug(
                    L"SteamVR overlay image loaded");
            }
        } else if (event.eventType == vr::VREvent_ImageFailed) {
            bool shouldLog = false;
            {
                std::scoped_lock lock(mutex_);
                refreshState_.MarkImageFailed(
                    std::chrono::steady_clock::now());
                shouldLog = !overlayImageFailureLogged_;
                overlayImageFailureLogged_ = true;
            }
            if (diagnostics_ && shouldLog) {
                diagnostics_->LogWarning(
                    L"SteamVR overlay image failed; retrying");
            }
        }
    }
}

bool VrOverlay::InitializeInput()
{
    auto* input = vr::VRInput();
    if (!input) {
        if (diagnostics_) {
            diagnostics_->LogWarning(L"SteamVR Input interface is unavailable");
        }
        return false;
    }

    const auto actionManifestPath = ExecutableDirectory() / Utf8ToWide(kActionManifestFile);
    if (!std::filesystem::exists(actionManifestPath)) {
        if (diagnostics_) {
            diagnostics_->LogError(L"SteamVR Input action manifest missing: " + actionManifestPath.wstring());
        }
        return false;
    }

    const vr::EVRInputError manifestError = input->SetActionManifestPath(WideToUtf8(actionManifestPath.wstring()).c_str());
    if (manifestError != vr::VRInputError_None) {
        LogInputErrorOnce(L"SetActionManifestPath", manifestError);
        return false;
    }

    struct InputHandleRequest {
        const char* path;
        uint64_t* handle;
        const wchar_t* action;
    };

    InputHandleRequest requests[] = {
        { kActionSetPath, &actionSetHandle_, L"GetActionSetHandle /actions/vrec" },
        { kPointerPoseActionPath, &pointerPoseAction_, L"GetActionHandle pointer_pose" },
        { kClickActionPath, &clickAction_, L"GetActionHandle click" },
        { kHapticActionPath, &hapticAction_, L"GetActionHandle haptic" },
        { kLeftHandPath, &leftHandSource_, L"GetInputSourceHandle left hand" },
        { kRightHandPath, &rightHandSource_, L"GetInputSourceHandle right hand" },
    };

    for (const auto& request : requests) {
        vr::EVRInputError error = vr::VRInputError_None;
        if (std::strstr(request.path, "/user/") == request.path) {
            error = input->GetInputSourceHandle(request.path, request.handle);
        } else if (std::strstr(request.path, "/actions/vrec/in/") == request.path ||
            std::strstr(request.path, "/actions/vrec/out/") == request.path) {
            error = input->GetActionHandle(request.path, request.handle);
        } else {
            error = input->GetActionSetHandle(request.path, request.handle);
        }

        if (error != vr::VRInputError_None || *request.handle == 0) {
            LogInputErrorOnce(request.action, error);
            return false;
        }
    }

    if (diagnostics_) {
        diagnostics_->LogDebug(L"SteamVR Input actions initialized");
    }
    return true;
}

void VrOverlay::UpdateControllerInteraction()
{
    if (!vr::VROverlay() || !IsValidOverlayHandle(overlayHandle_) || !mainOverlayVisible_ || !hideByAngleVisible_) {
        HideCursorOverlay();
        return;
    }

    const Hand attachedHand = SettingsSnapshot().overlay.hand;
    const Hand pointerHand = attachedHand == Hand::Left ? Hand::Right : Hand::Left;
    const vr::VRInputValueHandle_t pointerSourceHandle = pointerHand == Hand::Left ? leftHandSource_ : rightHandSource_;
    const vr::ETrackedControllerRole pointerRole = pointerHand == Hand::Left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;

    {
        std::scoped_lock lock(mutex_);
        if (!pointerPolicyLogged_ || lastPointerPolicyHand_ != attachedHand) {
            pointerPolicyLogged_ = true;
            lastPointerPolicyHand_ = attachedHand;
        }
    }

    if (attachedHand == Hand::Left) {
        lastLegacyLeftTrigger_ = false;
    } else {
        lastLegacyRightTrigger_ = false;
    }

    PointerHit best;
    bool inputPoseInactive = false;
    bool inputClickInactive = false;
    bool steamVrPoseAvailable = false;

    if (inputReady_ && vr::VRInput()) {
        vr::VRActiveActionSet_t actionSet{};
        actionSet.ulActionSet = actionSetHandle_;
        actionSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
        actionSet.ulSecondaryActionSet = vr::k_ulInvalidActionSetHandle;
        actionSet.nPriority = 0;

        const vr::EVRInputError updateError = vr::VRInput()->UpdateActionState(&actionSet, sizeof(actionSet), 1);
        if (updateError != vr::VRInputError_None) {
            LogInputErrorOnce(L"UpdateActionState", updateError);
        } else {
            if (pointerSourceHandle == vr::k_ulInvalidInputValueHandle) {
                inputPoseInactive = true;
            } else {
                const auto sourceHandle = pointerSourceHandle;

                vr::InputPoseActionData_t poseData{};
                const vr::EVRInputError poseError = vr::VRInput()->GetPoseActionDataForNextFrame(
                    pointerPoseAction_,
                    vr::TrackingUniverseStanding,
                    &poseData,
                    sizeof(poseData),
                    sourceHandle);
                if (poseError != vr::VRInputError_None) {
                    LogInputErrorOnce(L"GetPoseActionDataForNextFrame pointer_pose", poseError);
                    inputPoseInactive = true;
                } else if (!poseData.bActive || !poseData.pose.bPoseIsValid) {
                    inputPoseInactive = true;
                } else {
                    steamVrPoseAvailable = true;
                    if (diagnostics_ && !steamVrStrictRayLogged_) {
                        diagnostics_->LogDebug(L"Overlay hover uses SteamVR Input strict pointer ray");
                        steamVrStrictRayLogged_ = true;
                    }

                    PointerHit candidate;
                    if (ComputeStrictOverlayHit(overlayHandle_, poseData.pose.mDeviceToAbsoluteTracking, candidate)) {
                        vr::InputDigitalActionData_t clickData{};
                        const vr::EVRInputError clickError = vr::VRInput()->GetDigitalActionData(
                            clickAction_,
                            &clickData,
                            sizeof(clickData),
                            sourceHandle);
                        if (clickError != vr::VRInputError_None) {
                            LogInputErrorOnce(L"GetDigitalActionData click", clickError);
                        } else if (!clickData.bActive) {
                            inputClickInactive = true;
                        } else {
                            candidate.clickDown = clickData.bState;
                            candidate.clickChanged = clickData.bChanged;
                        }

                        candidate.steamVrInput = true;
                        candidate.inputSource = sourceHandle;
                        if (IsBetterPointerHit(candidate, best)) {
                            best = candidate;
                        }
                    }
                }
            }
        }
    }

    if (steamVrPoseAvailable && !best.hit) {
        {
            std::scoped_lock lock(mutex_);
            cursorVisible_ = false;
            cursorPressed_ = false;
        }
        HideCursorOverlay();
        return;
    }

    if (!steamVrPoseAvailable && inputPoseInactive && diagnostics_ && !poseInactiveLogged_) {
        diagnostics_->LogDebug(L"SteamVR Input pointer_pose is inactive or invalid; legacy strict ray is available");
        poseInactiveLogged_ = true;
    } else if (best.hit) {
        poseInactiveLogged_ = false;
    }

    if (inputClickInactive && diagnostics_ && !clickInactiveLogged_) {
        diagnostics_->LogDebug(L"SteamVR Input click action is inactive; trigger clicks may be unavailable");
        clickInactiveLogged_ = true;
    } else if (!inputClickInactive) {
        clickInactiveLogged_ = false;
    }

    if (!steamVrPoseAvailable && vr::VRSystem()) {
        if (diagnostics_ && !legacyFallbackReasonLogged_) {
            diagnostics_->LogDebug(L"Using legacy strict pointer ray because SteamVR Input pose is unavailable");
            legacyFallbackReasonLogged_ = true;
        }

        auto tryLegacyController = [&](vr::ETrackedControllerRole role, bool& lastTrigger) {
            if (role != pointerRole) {
                lastTrigger = false;
                return;
            }

            const vr::TrackedDeviceIndex_t controller = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(role);
            if (controller == vr::k_unTrackedDeviceIndexInvalid || !vr::VRSystem()->IsTrackedDeviceConnected(controller)) {
                lastTrigger = false;
                return;
            }

            vr::VRControllerState_t state{};
            vr::TrackedDevicePose_t pose{};
            if (!vr::VRSystem()->GetControllerStateWithPose(vr::TrackingUniverseStanding, controller, &state, sizeof(state), &pose) ||
                !pose.bPoseIsValid) {
                lastTrigger = false;
                return;
            }

            const bool triggerDown =
                (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0 ||
                state.rAxis[1].x > 0.75f;
            const bool triggerChanged = triggerDown && !lastTrigger;
            lastTrigger = triggerDown;

            PointerHit candidate;
            if (!ComputeStrictOverlayHit(overlayHandle_, pose.mDeviceToAbsoluteTracking, candidate)) {
                return;
            }

            if (diagnostics_ && !legacyStrictRayLogged_) {
                diagnostics_->LogDebug(L"Overlay hover uses legacy strict pointer ray");
                legacyStrictRayLogged_ = true;
            }

            candidate.controller = controller;
            candidate.clickDown = triggerDown;
            candidate.clickChanged = triggerChanged;
            if (IsBetterPointerHit(candidate, best)) {
                best = candidate;
            }
        };

        tryLegacyController(vr::TrackedControllerRole_LeftHand, lastLegacyLeftTrigger_);
        tryLegacyController(vr::TrackedControllerRole_RightHand, lastLegacyRightTrigger_);
    }

    if (!best.hit) {
        {
            std::scoped_lock lock(mutex_);
            cursorVisible_ = false;
            cursorPressed_ = false;
        }
        HideCursorOverlay();
        return;
    }

    {
        std::scoped_lock lock(mutex_);
        cursorVisible_ = true;
        cursorPressed_ = best.clickDown;
        cursorX_ = best.x;
        cursorY_ = best.yTopLeft;
    }

    if (IsValidOverlayHandle(cursorOverlayHandle_)) {
        vr::HmdVector2_t overlayCoordinates{};
        overlayCoordinates.v[0] = best.x;
        overlayCoordinates.v[1] = best.yOpenVr;

        vr::HmdMatrix34_t cursorTransform{};
        const vr::EVROverlayError transformError = vr::VROverlay()->GetTransformForOverlayCoordinates(
            overlayHandle_,
            vr::TrackingUniverseStanding,
            overlayCoordinates,
            &cursorTransform);
        LogOverlayError(diagnostics_, L"GetTransformForOverlayCoordinates", transformError);
        if (transformError == vr::VROverlayError_None) {
            cursorTransformFailureLogged_ = false;
            const vr::HmdVector3_t lift = CursorLiftDirection(cursorTransform, best.source);
            cursorTransform.m[0][3] += lift.v[0] * kCursorLiftMeters;
            cursorTransform.m[1][3] += lift.v[1] * kCursorLiftMeters;
            cursorTransform.m[2][3] += lift.v[2] * kCursorLiftMeters;
            LogOverlayError(diagnostics_, L"SetCursorOverlayTransformAbsolute", vr::VROverlay()->SetOverlayTransformAbsolute(cursorOverlayHandle_, vr::TrackingUniverseStanding, &cursorTransform));
            if (!cursorOverlayVisible_) {
                LogOverlayError(diagnostics_, L"ShowCursorOverlay", vr::VROverlay()->ShowOverlay(cursorOverlayHandle_));
                cursorOverlayVisible_ = true;
            }
        } else {
            if (diagnostics_ && !cursorTransformFailureLogged_) {
                diagnostics_->LogDebug(L"Cursor hidden because overlay coordinate transform failed");
                cursorTransformFailureLogged_ = true;
            }
            {
                std::scoped_lock lock(mutex_);
                cursorVisible_ = false;
                cursorPressed_ = false;
            }
            HideCursorOverlay();
            return;
        }
    }

    if (best.clickDown &&
        best.clickChanged &&
        PtInRectPixels(RecButtonRect(), best.x, best.yTopLeft) &&
        recordingCommand_) {
        const StatusSnapshot status =
            statusProvider_ ? statusProvider_() : StatusSnapshot{};
        const bool accepted =
            status.obsConnState == ObsConnState::Connected &&
            recordingCommand_(
                status.recorderState != RecorderState::Recording);
        if (!accepted) {
            return;
        }
        if (best.steamVrInput && vr::VRInput() && hapticAction_ != vr::k_ulInvalidActionHandle) {
            const vr::EVRInputError hapticError = vr::VRInput()->TriggerHapticVibrationAction(hapticAction_, 0.0f, 0.025f, 120.0f, 0.45f, best.inputSource);
            LogInputErrorOnce(L"TriggerHapticVibrationAction", hapticError);
        } else if (vr::VRSystem() && best.controller != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRSystem()->TriggerHapticPulse(best.controller, 0, 600);
        }
    }
}

void VrOverlay::HideCursorOverlay()
{
    if (!vr::VROverlay() || !IsValidOverlayHandle(cursorOverlayHandle_) || !cursorOverlayVisible_) {
        return;
    }

    LogOverlayError(diagnostics_, L"HideCursorOverlay", vr::VROverlay()->HideOverlay(cursorOverlayHandle_));
    cursorOverlayVisible_ = false;
}

void VrOverlay::LogInputErrorOnce(const wchar_t* action, int error)
{
    if (!diagnostics_ || error == vr::VRInputError_None) {
        return;
    }

    const uint32_t code = static_cast<uint32_t>(error);
    const std::wstring context(action);
    if (lastInputErrorCode_ == code && lastInputErrorContext_ == context) {
        return;
    }

    lastInputErrorCode_ = code;
    lastInputErrorContext_ = context;
    diagnostics_->LogWarning(context + L" failed: " + InputErrorToString(static_cast<vr::EVRInputError>(error)));
}

void VrOverlay::RenderOverlay(const StatusSnapshot& status)
{
    const Settings settings = SettingsSnapshot();
    const bool debugChecker = DebugOverlayCheckerEnabled();
    const OverlayVisualState visualState = MakeOverlayVisualState(
        status,
        settings.language,
        debugChecker);
    if (renderer_.Render(
            overlayHandle_,
            status,
            settings.language,
            debugChecker,
            diagnostics_)) {
        std::scoped_lock lock(mutex_);
        refreshState_.MarkSubmitted(
            visualState,
            std::chrono::steady_clock::now());
        if (diagnostics_) {
            diagnostics_->LogDebug(
                L"SteamVR overlay image submitted");
        }
    }
}

void VrOverlay::ApplyManifest()
{
    auto* apps = vr::VRApplications();
    if (!apps) {
        return;
    }

    const auto manifest = ExecutableDirectory() / L"app.vrmanifest";
    if (!std::filesystem::exists(manifest)) {
        return;
    }

    const bool installed = apps->IsApplicationInstalled(kAppKey);
    bool isDashboardOverlay = false;
    if (installed) {
        vr::EVRApplicationError propertyError = vr::VRApplicationError_None;
        isDashboardOverlay = apps->GetApplicationPropertyBool(
            kAppKey,
            vr::VRApplicationProperty_IsDashboardOverlay_Bool,
            &propertyError) &&
            propertyError == vr::VRApplicationError_None;
    }

    const bool currentPath = installed && RegisteredApplicationMatchesCurrentPath(apps, ExecutableDirectory());
    if (installed && isDashboardOverlay && currentPath) {
        if (!manifestApplied_ && diagnostics_) {
            diagnostics_->LogDebug(L"OpenVR manifest already registered");
        }
        manifestApplied_ = true;
        apps->IdentifyApplication(0, kAppKey);
        return;
    }

    const auto manifestUtf8 = WideToUtf8(manifest.wstring());
    if (installed) {
        RemoveRegisteredApplicationManifest(apps, manifest);
    }

    vr::EVRApplicationError err = apps->AddApplicationManifest(manifestUtf8.c_str(), false);
    if (err == vr::VRApplicationError_AppKeyAlreadyExists) {
        RemoveRegisteredApplicationManifest(apps, manifest);
        err = apps->AddApplicationManifest(manifestUtf8.c_str(), false);
    }

    if (err == vr::VRApplicationError_None ||
        err == vr::VRApplicationError_AppKeyAlreadyExists) {
        manifestApplied_ = true;
        manifestErrorLogged_ = false;
        apps->IdentifyApplication(0, kAppKey);
        if (diagnostics_) {
            diagnostics_->LogInfo(L"OpenVR manifest registered");
        }
    } else if (diagnostics_ && !manifestErrorLogged_) {
        manifestErrorLogged_ = true;
        diagnostics_->LogWarning(L"AddApplicationManifest failed: " + ApplicationErrorToString(apps, err));
    }
}

void VrOverlay::SetError(std::wstring message)
{
    std::wstring errorToLog;
    {
        std::scoped_lock lock(mutex_);
        lastError_ = std::move(message);
        if (lastLoggedOverlayError_ != lastError_) {
            lastLoggedOverlayError_ = lastError_;
            errorToLog = lastError_;
        }
    }
    if (diagnostics_ && !errorToLog.empty()) {
        diagnostics_->LogError(L"SteamVR overlay: " + errorToLog);
    }
}
