#include "Disk.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

namespace disk {

std::filesystem::path saveDir() {
    return Mod::get()->getSaveDir();
}

Result<ByteVector> readBinary(std::filesystem::path const& path) {
    return file::readBinary(path);
}

Result<std::string> readString(std::filesystem::path const& path) {
    return file::readString(path);
}

Result<matjson::Value> readJson(std::filesystem::path const& path) {
    return file::readJson(path);
}

Result<> writeBinary(std::filesystem::path const& path, ByteSpan data) {
    return file::writeBinary(path, data);
}

Result<> writeString(std::filesystem::path const& path, std::string_view data) {
    return file::writeString(path, data);
}

Result<> writeBinarySafe(std::filesystem::path const& path, ByteSpan data) {
    return file::writeBinarySafe(path, data);
}

Result<> writeStringSafe(std::filesystem::path const& path, std::string_view data) {
    return file::writeStringSafe(path, data);
}

std::string pathString(std::filesystem::path const& path) {
    return utils::string::pathToString(path);
}

}
