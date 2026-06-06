#pragma once

#include "AppTypes.h"
#include "Diagnostics.h"

#include <filesystem>

class SettingsStore {
public:
    explicit SettingsStore(Diagnostics* diagnostics);
    SettingsStore(std::filesystem::path path, Diagnostics* diagnostics);

    Settings Load();
    bool Save(const Settings& settings);
    const std::filesystem::path& Path() const { return path_; }

    static Settings Defaults();
    static void Normalize(Settings& settings);

private:
    std::filesystem::path path_;
    Diagnostics* diagnostics_ = nullptr;
};
