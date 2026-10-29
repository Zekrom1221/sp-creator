cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(SPCreator VERSION 0.9.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/SPList.cpp
    src/Storage.cpp
    src/StorageList.cpp
    src/Text.cpp
    src/Transfer.cpp
)

# The smoke test (src/Smoke.inl): development builds only, never a release.
option(SPC_SMOKE "Build the smoke test in" OFF)
if (SPC_SMOKE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SPC_SMOKE)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
