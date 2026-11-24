#pragma once

#include <Geode/Result.hpp>
#include <Geode/utils/general.hpp>
#include <matjson.hpp>

#include <filesystem>
#include <string>
#include <string_view>

// Geode's file functions by std::filesystem paths, as the rest of the mod has them, and errors as text.
// The one place that knows how Geode takes a path: Geode 5 takes std::filesystem::path itself; Geode 6 its
// own asp::fs::Path (no conversion from std::filesystem::path) and gives an error type of its own.
namespace disk {
    // The mod's save folder.
    std::filesystem::path saveDir();

    geode::Result<geode::ByteVector> readBinary(std::filesystem::path const& path);
    geode::Result<std::string> readString(std::filesystem::path const& path);
    geode::Result<matjson::Value> readJson(std::filesystem::path const& path);

    geode::Result<> writeBinary(std::filesystem::path const& path, geode::ByteSpan data);
    geode::Result<> writeString(std::filesystem::path const& path, std::string_view data);
    // Through a file beside it, so a write that fails leaves the old file whole.
    geode::Result<> writeBinarySafe(std::filesystem::path const& path, geode::ByteSpan data);
    geode::Result<> writeStringSafe(std::filesystem::path const& path, std::string_view data);

    // As UTF-8 text.
    std::string pathString(std::filesystem::path const& path);
}
