cmake_minimum_required(VERSION 3.28)
project(kaepora LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

include(shipwright/CMake/soh-cvars.cmake)

# shipwright/ holds only the upstream files this build reads, so every source in it is compiled.
file(GLOB_RECURSE SHIPWRIGHT_SOURCES CONFIGURE_DEPENDS shipwright/*.c shipwright/*.cpp)

add_executable(kaepora-seedgen
    generator/main.cpp
    generator/build_info.c
    generator/cvars.cpp
    generator/game_state.cpp
    generator/remake.cpp
    generator/traps.cpp
    ${SHIPWRIGHT_SOURCES}
)
target_include_directories(kaepora-seedgen PRIVATE
    .
    shipwright/soh/include
    shipwright/soh/src
    shipwright/soh/assets
    shipwright/soh
    shipwright/libultraship/include
)
# functions.h needs a level for game prints. 6 turns them off, as in SoH.
target_compile_definitions(kaepora-seedgen PRIVATE LOG_LEVEL_GAME_PRINTS=6)
# The item icon table drops const from texture names. The generator only reads the slot tables next to it.
set_source_files_properties(shipwright/soh/src/code/z_inventory.c PROPERTIES COMPILE_OPTIONS -Wno-discarded-qualifiers)
target_link_libraries(kaepora-seedgen PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

enable_testing()
add_test(NAME upstream-tool COMMAND python3 -I ${PROJECT_SOURCE_DIR}/tests/upstream_test.py)
foreach(TEST IN ITEMS only_shuffled_chests_change plan_lists_each_shuffled_chest plan_matches_spoiler
                     same_seed_same_files twenty_seeds_generate spirit_dungeon_items_stay_out_of_the_colossus
                     fixed_seed_matches_fixture)
    add_test(NAME ${TEST} COMMAND python3 -I ${PROJECT_SOURCE_DIR}/tests/seedgen_test.py
                                  $<TARGET_FILE:kaepora-seedgen> SeedgenTest.test_${TEST})
endforeach()
