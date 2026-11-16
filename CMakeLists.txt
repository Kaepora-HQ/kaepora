cmake_minimum_required(VERSION 3.28)
project(kaepora LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(zstd REQUIRED)

include(shipwright/CMake/soh-cvars.cmake)

# shipwright/ holds only the upstream files this build reads, so every source in it is compiled.
file(GLOB_RECURSE SHIPWRIGHT_SOURCES CONFIGURE_DEPENDS shipwright/*.c shipwright/*.cpp)

# Everything but the two programs' main files, compiled once for both.
add_library(kaepora OBJECT
    generator/build_info.c
    generator/cvars.cpp
    generator/game_state.cpp
    generator/remake.cpp
    generator/setup.cpp
    generator/traps.cpp
    ${SHIPWRIGHT_SOURCES}
)
target_include_directories(kaepora PUBLIC
    .
    shipwright/soh/include
    shipwright/soh/src
    shipwright/soh/assets
    shipwright/soh
    shipwright/libultraship/include
)
# functions.h needs a level for game prints. 6 turns them off, as in SoH.
target_compile_definitions(kaepora PUBLIC LOG_LEVEL_GAME_PRINTS=6)
# Shop prices use floating point. SoH builds without fused multiply-add, so the same seed gives the same prices.
target_compile_options(kaepora PRIVATE -ffp-contract=off)
# The item icon table drops const from texture names. The generator only reads the slot tables next to it.
set_source_files_properties(shipwright/soh/src/code/z_inventory.c PROPERTIES COMPILE_OPTIONS -Wno-discarded-qualifiers)
target_link_libraries(kaepora PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(kaepora-seedgen generator/main.cpp)
target_link_libraries(kaepora-seedgen PRIVATE kaepora)

# Answers whether a location can be reached in a given world, for the logic tests.
add_executable(kaepora-reach generator/reach.cpp)
target_link_libraries(kaepora-reach PRIVATE kaepora)

# Designated initializers leave out the members that start empty, which -Wextra would report.
set(PATCHER_WARNINGS -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

add_library(kaepora-patch-files STATIC patcher/byml.cpp patcher/compression.cpp patcher/romfs.cpp)
target_include_directories(kaepora-patch-files PUBLIC patcher)
target_link_libraries(kaepora-patch-files PRIVATE zstd::libzstd)
target_compile_options(kaepora-patch-files PRIVATE ${PATCHER_WARNINGS})

add_executable(kaepora-patch patcher/main.cpp)
target_link_libraries(kaepora-patch PRIVATE kaepora-patch-files nlohmann_json::nlohmann_json)
target_compile_options(kaepora-patch PRIVATE ${PATCHER_WARNINGS})

add_executable(kaepora-patch-roundtrip tests/placement_roundtrip.cpp)
target_link_libraries(kaepora-patch-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-roundtrip PRIVATE ${PATCHER_WARNINGS})

enable_testing()
add_test(NAME upstream-tool COMMAND python3 -I ${PROJECT_SOURCE_DIR}/tests/upstream_test.py)
foreach(TEST IN ITEMS only_shuffled_chests_change plan_lists_each_shuffled_chest plan_matches_spoiler
                     same_seed_same_files twenty_seeds_generate spirit_dungeon_items_stay_out_of_the_colossus
                     fixed_seed_matches_fixture)
    add_test(NAME ${TEST} COMMAND python3 -I ${PROJECT_SOURCE_DIR}/tests/seedgen_test.py
                                  $<TARGET_FILE:kaepora-seedgen> SeedgenTest.test_${TEST})
endforeach()
add_test(NAME logic COMMAND python3 -I ${PROJECT_SOURCE_DIR}/tests/logic_test.py $<TARGET_FILE:kaepora-reach>)

# The game's container is Nintendo's data and stays out of git, so the tests that read it need its path.
set(KAEPORA_GAME_NUP "" CACHE FILEPATH "The game's .nup container, read by the patcher tests")
set(KAEPORA_TESTED_MOD "" CACHE FILEPATH "The placement file of the chest-swap mod tested in the game")
set(PATCH_TEST python3 -I ${PROJECT_SOURCE_DIR}/tests/patch_test.py)
if(KAEPORA_GAME_NUP)
    add_test(NAME patch-roundtrip COMMAND kaepora-patch-roundtrip "${KAEPORA_GAME_NUP}")
    foreach(test larger-plan bad-input size-table size-table-unchanged)
        add_test(NAME patch-${test} COMMAND ${PATCH_TEST} ${test} $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}")
    endforeach()
    if(KAEPORA_TESTED_MOD)
        add_test(NAME patch-tested-mod
            COMMAND ${PATCH_TEST} tested-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}" "${KAEPORA_TESTED_MOD}")
    endif()
endif()
