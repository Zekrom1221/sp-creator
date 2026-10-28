#pragma once

#include "Core.hpp"
#include "Storage.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Export and import of SPs. A .spcreator file holds pack.json (the level and version the SPs are for,
// their names) and the SPs' files as saved, seal and all: the checkpoints can't be made again without
// playing to them, so they go as they are. Not a zip: Geode's zip can't write in memory, and a plain
// row of parts (magic, then size + bytes for pack.json and each SP) is simple to read with strict limits.
namespace transfer {
    inline constexpr char const* EXTENSION = ".spcreator";

    // A file's parts: pack.json, and the SP files in its "sps" order.
    struct Raw {
        matjson::Value manifest;
        std::vector<geode::ByteVector> files;
    };
    geode::ByteVector join(Raw const& raw);
    // The error is for the log.
    geode::Result<Raw> split(geode::ByteSpan bytes);

    // The file of these SPs of the open level (SPs not saved yet get saved): `packed` of them made it.
    // The error is for the player.
    geode::Result<geode::ByteVector> pack(std::vector<core::SP> const& sps, int& packed);

    struct Item {
        geode::ByteVector file;
        storage::Header header;
        std::string name;
        bool here = false;       // the same as one of the level's SPs: adding skips it, replacing takes it
        bool repeated = false;   // the same as an item before it: never taken
        int nearby = 0;          // position of the level's SP close to it (1 on), 0 if none
        float nearbyPercent = 0;
    };

    struct Pack {
        std::vector<Item> items;  // by position
        int damaged = 0;          // SPs in the file that can't be used
        bool sameAsLevel = false; // the level has these SPs and no others
    };

    // Reads a file and checks it against the open level. The error is for the player.
    geode::Result<Pack> open(std::filesystem::path const& path);
    // A version of the level's SPs, checked the same way.
    geode::Result<Pack> openVersion(storage::Version const& version);
    // Adds the picked items to the level, or puts them in place of its own SPs.
    core::ImportResult take(Pack const& pack, std::vector<bool> const& picked, core::Replace replace);

    // The system's file dialog, from the Downloads folder: a file to save the SPs of `levelName` to, or to
    // import from. `then` gets it on the main thread, empty if cancelled. The dialog runs beside the game, as
    // Geode's own do: with the game stopped, the clicks made in the dialog would reach the game once it
    // closed. False (and no dialog) while one is open already.
    bool chooseFile(bool save, std::string const& levelName, geode::Function<void(std::optional<std::filesystem::path>)> then);
}
