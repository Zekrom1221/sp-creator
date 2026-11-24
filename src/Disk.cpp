#include "Disk.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <asp/fs/std.hpp>

using namespace geode::prelude;

// Geode 6: its file functions take asp::fs::Path (UTF-8 inside), made here from the mod's std::filesystem paths
// through their wide form, and give FileError: as text, as the mod has its errors.
namespace disk {

namespace {

asp::fs::Path pathOf(std::filesystem::path const& path) {
    return asp::fs::Path::fromStdPath(path);
}

template <class T>
Result<T> textError(Result<T, file::FileError> res) {
    if (res.isErr()) return Err(res.unwrapErr().toString());
    if constexpr (std::is_void_v<T>) return Ok();
    else return Ok(std::move(res).unwrap());
}

}

std::filesystem::path saveDir() {
    return Mod::get()->getSaveDir().toStdPath().unwrapOr(std::filesystem::path());
}

Result<ByteVector> readBinary(std::filesystem::path const& path) {
    return textError(file::readBinary(pathOf(path)));
}

Result<std::string> readString(std::filesystem::path const& path) {
    return textError(file::readString(pathOf(path)));
}

Result<matjson::Value> readJson(std::filesystem::path const& path) {
    return file::readJson(pathOf(path));
}

Result<> writeBinary(std::filesystem::path const& path, ByteSpan data) {
    return textError(file::writeBinary(pathOf(path), data));
}

Result<> writeString(std::filesystem::path const& path, std::string_view data) {
    return textError(file::writeString(pathOf(path), data));
}

Result<> writeBinarySafe(std::filesystem::path const& path, ByteSpan data) {
    return textError(file::writeBinarySafe(pathOf(path), data));
}

Result<> writeStringSafe(std::filesystem::path const& path, std::string_view data) {
    return textError(file::writeStringSafe(pathOf(path), data));
}

std::string pathString(std::filesystem::path const& path) {
    return pathOf(path).string();
}

std::filesystem::path pathFromString(std::string const& utf8) {
    return asp::fs::Path(utf8).toStdPath().unwrapOr(std::filesystem::path());
}

}
