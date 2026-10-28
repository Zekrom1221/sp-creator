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
        bool duplicate = false;  // the same as one of the level's SPs, or as an item before it
        int nearby = 0;          // position of the level's SP close to it (1 on), 0 if none
        float nearbyPercent = 0;
    };

    struct Pack {
        std::vector<Item> items;      // by position
        int damaged = 0;              // SPs in the file that can't be used
        std::vector<core::SP> inFile;  // the level's SPs the file holds too: replacing keeps them
    };

    // Reads a file and checks it against the open level. The error is for the player.
    geode::Result<Pack> open(std::filesystem::path const& path);
    // Adds the picked items to the level; with `replace`, in place of its own SPs.
    core::ImportResult take(Pack const& pack, std::vector<bool> const& picked, bool replace);

    // The system's file dialog, from the Downloads folder: a file to save the SPs of `levelName` to, or to
    // import from. Waits for the player (the game stands still meanwhile); empty if cancelled.
    std::optional<std::filesystem::path> chooseFile(bool save, std::string const& levelName = "");
}
