#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

// What an SP is made of, shared by the gameplay code (main.cpp) and saving (Storage.cpp).

struct PlayerFrame {
    float y = 0;
    double yVel = 0;
    float rot = 0;
};

// State before one physics step.
struct Frame {
    float x = 0;
    PlayerFrame p1, p2;
    bool dual = false;
    // Only compared with, never forced (the camera of the first frame is put back after loading).
    float camX = 0, camY = 0;
    // Debug only, not saved.
    float speed = 0;
    double time = 0;
    double probeY = 0;  // height of the first object of the smoke test's probe group
    int hits = 0;       // destroyPlayer calls so far in the attempt (several per death; noclip goes on)
};

struct Input {
    int step = 0;  // goes in before this step
    int button = 1;
    bool p1 = true;
    bool down = false;
    uint64_t seq = 0;  // order of all inputs in the attempt
};

// Buttons held, by holdIndex: 1-3 player 1 (jump, left, right), 4-6 player 2.
using Holds = std::array<bool, 7>;

inline int holdIndex(int button, bool p1) {
    return std::clamp(button, 1, 3) + (p1 ? 0 : 3);
}

struct StartPoint {
    int id = 0;
    geode::Ref<CheckpointObject> cp;
    Holds holds{};              // buttons held when the checkpoint was taken
    std::vector<Frame> frames;  // frames[k]: state before auto-part step k; the player controls from frames.size()
    std::vector<Input> inputs;  // steps relative to the spawn
    std::vector<Frame> tail;    // the original run right after the handoff, for the comparison (not saved)
    float x = 0, percent = 0;   // where the player takes control
    int spawnStep = 0;          // step of the original attempt the checkpoint was taken at
    std::string name;           // given by the player, empty: shown by its position
    // Its startpos object in the level (main.cpp), at x: other mods that look for startposes find it.
    geode::Ref<StartPosObject> object;

    int handoff() const { return static_cast<int>(frames.size()); }
};
