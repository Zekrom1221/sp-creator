#pragma once

// SP files of GD 2.2081 (format 2): their checkpoint, as PersistenceAPI wrote it, read into a checkpoint of this game
// (Convert2081.cpp, generated). Storage.cpp then writes the SP again in this game's format.

#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/GameObject.hpp>

#include <cstdint>
#include <span>

#ifdef SPC_SMOKE
#include <matjson.hpp>
#endif

namespace convert2081 {

// The level objects a checkpoint of 2.2081 refers to, by their place among the level's objects as PersistenceAPI
// gave it: 0 on, -1 none, -2 and -3 the players. An object that isn't there (a place past the last one) throws.
struct Objects {
    virtual ~Objects() = default;
    virtual GameObject* at(int place) const = 0;
    // The other way round, for the smoke test's dump.
    virtual int placeOf(GameObject* obj) const = 0;
};

// The checkpoint at `at` in `bytes`; `at` goes past it. Throws if the bytes don't hold one (out_of_range: they end
// before it does). Autoreleased.
CheckpointObject* readCheckpoint(std::span<uint8_t const> bytes, size_t& at, Objects const& objects);

#ifdef SPC_SMOKE
// What it holds under the names the Python reader (converter/checkpoint2081.py) gives the fields, for comparing.
matjson::Value dumpCheckpoint(CheckpointObject* cp, Objects const& objects);
#endif

}  // namespace convert2081
