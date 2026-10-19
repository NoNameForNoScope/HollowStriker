cmake_minimum_required(VERSION 3.16)
project(BodycamMini VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /O2 /fp:precise /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2 -ffp-contract=off)
endif()

# Nucleul (simulare determinista + randare software + retea) - fara dependinte externe
add_library(bodycam_core STATIC
    src/core/rng.hpp
    src/core/hash.hpp
    src/core/events.hpp
    src/core/csv.hpp
    src/core/csv.cpp
    src/core/ini.hpp
    src/core/ini.cpp
    src/core/profiler.hpp
    src/sim/surface.hpp
    src/sim/map.hpp
    src/sim/map.cpp
    src/sim/lightmap.hpp
    src/sim/lightmap.cpp
    src/sim/los.hpp
    src/sim/los.cpp
    src/sim/paths.hpp
    src/sim/paths.cpp
    src/sim/params.hpp
    src/sim/fsm.hpp
    src/sim/agent.hpp
    src/sim/weapon.hpp
    src/sim/weapon.cpp
    src/sim/weapon_data.hpp
    src/sim/weapon_data.cpp
    src/sim/weapon_stats.hpp
    src/sim/weapon_stats.cpp
    src/sim/character_profile.hpp
    src/sim/character_profile.cpp
    src/sim/pawn_config.cpp
    src/sim/physiology_timeline.cpp
    src/core/asset_manifest.cpp
    src/sim/weapon_mod.hpp
    src/sim/ballistics.hpp
    src/sim/ballistics.cpp
    src/sim/perception.hpp
    src/sim/perception.cpp
    src/sim/physics_stance.hpp
    src/sim/physiology.hpp
    src/sim/physiology.cpp
    src/sim/health.hpp
    src/sim/reload.hpp
    src/sim/reload.cpp
    src/sim/interaction.hpp
    src/sim/interaction.cpp
    src/sim/loadout.hpp
    src/sim/tablet.cpp
    src/sim/equipment.cpp
    src/sim/player_sim.hpp
    src/sim/player_sim.cpp
    src/sim/pawns.hpp
    src/sim/pawns.cpp
    src/sim/props.hpp
    src/sim/props.cpp
    src/sim/game_state.hpp
    src/sim/world.hpp
    src/sim/world.cpp
    src/sim/shooting_range.hpp
    src/sim/shooting_range.cpp
    src/sim/bot_ai.cpp
    src/sim/spatial_audio_engine.hpp
    src/game/scenario.hpp
    src/game/scenario.cpp
    src/game/map_catalog.hpp
    src/game/map_catalog.cpp
    src/game/campaign.hpp
    src/game/campaign.cpp
    src/stats/records.hpp
    src/stats/data_storage.hpp
    src/stats/data_storage.cpp
    src/stats/analytics.hpp
    src/stats/analytics.cpp
    src/stats/statistics_system.hpp
    src/stats/statistics_system.cpp
    src/stats/experiments.hpp
    src/stats/experiments.cpp
    src/render/frame.hpp
    src/render/textures.hpp
    src/render/font.hpp
    src/render/fx.hpp
    src/render/fx.cpp
    src/render/math3d.hpp
    src/render/mesh.hpp
    src/render/mesh.cpp
    src/render/character.hpp
    src/render/character.cpp
    src/render/raycaster.hpp
    src/render/raycaster.cpp
    src/render/range_render.cpp
    src/render/body_camera.hpp
    src/render/body_camera.cpp
    src/render/viewmodel.hpp
    src/render/viewmodel.cpp
    src/render/weapon_mesh.hpp
    src/render/weapon_mesh.cpp
    src/render/pawn_mesh.cpp
    src/render/obj_model.cpp
    src/render/postfx.hpp
    src/render/postfx.cpp
    src/render/hud.hpp
    src/render/hud.cpp
    src/render/tablet_ui.hpp
    src/render/tablet_ui.cpp
    src/render/tablet_canvas.hpp
    src/render/tablet_stats_ui.cpp
    src/experiment/runner.hpp
    src/experiment/runner.cpp
    src/audio/dsp.hpp
    src/audio/breath_synth.hpp
    src/audio/breath_synth.cpp
)
target_include_directories(bodycam_core PUBLIC src)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(bodycam_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(bodycam_exp src/cli/exp_main.cpp)
target_link_libraries(bodycam_exp PRIVATE bodycam_core)

option(BODYCAM_BUILD_VIEWER "Build viewer cu SDL2" ON)
if(BODYCAM_BUILD_VIEWER)
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

    add_executable(bodycam_app
        src/app/main.cpp
        src/app/game_view.hpp
        src/app/game_view.cpp
        src/app/app_session.hpp
        src/app/app_session.cpp
        src/app/net_session.hpp
        src/app/net_session.cpp
        src/audio/audio_engine.hpp
        src/audio/audio_engine.cpp
    )
    target_link_libraries(bodycam_app PRIVATE bodycam_core SDL2::SDL2main SDL2::SDL2-static)
endif()

add_executable(bodycam_tests tests/test_main.cpp)
target_link_libraries(bodycam_tests PRIVATE bodycam_core)
