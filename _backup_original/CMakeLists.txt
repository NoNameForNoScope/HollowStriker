cmake_minimum_required(VERSION 3.16)
project(BodycamMini VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /O2 /fp:precise)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2 -ffp-contract=off)
endif()

add_library(bodycam_core STATIC
    src/core/rng.hpp
    src/core/hash.hpp
    src/core/events.hpp
    src/core/csv.hpp
    src/core/csv.cpp
    src/core/ini.hpp
    src/core/ini.cpp
    src/sim/map.hpp
    src/sim/map.cpp
    src/sim/los.hpp
    src/sim/los.cpp
    src/sim/paths.hpp
    src/sim/paths.cpp
    src/sim/params.hpp
    src/sim/fsm.hpp
    src/sim/agent.hpp
    src/sim/weapon.hpp
    src/sim/weapon.cpp
    src/sim/perception.hpp
    src/sim/perception.cpp
    src/sim/world.hpp
    src/sim/world.cpp
    src/experiment/runner.hpp
    src/experiment/runner.cpp
    src/render/raycaster.hpp
    src/render/raycaster.cpp
    src/render/hud.hpp
    src/render/hud.cpp
    src/sim/camera.hpp
    src/sim/ballistics.hpp
    src/sim/weapon_mod.hpp
    src/sim/audio.hpp
    src/sim/physics_stance.hpp
    src/sim/true_body.hpp
    src/sim/tactical_master_system.hpp
)
target_include_directories(bodycam_core PUBLIC src)

add_executable(bodycam_exp src/cli/exp_main.cpp)
target_link_libraries(bodycam_exp PRIVATE bodycam_core)

option(BODYCAM_BUILD_VIEWER "Build viewer cu SDL2" ON)
if(BODYCAM_BUILD_VIEWER)
    # Setari de politica ?i op?iuni obligatorii pentru SDL2 pe CMake modern
    if(POLICY CMP0077)
        cmake_policy(SET CMP0077 NEW)
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.28.5
    )
    FetchContent_MakeAvailable(SDL2)

    add_executable(bodycam_app src/app/main.cpp)
    target_link_libraries(bodycam_app PRIVATE bodycam_core SDL2::SDL2main SDL2::SDL2-static)
endif()

add_executable(bodycam_tests tests/test_main.cpp)
target_link_libraries(bodycam_tests PRIVATE bodycam_core)
