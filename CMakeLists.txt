cmake_minimum_required(VERSION 3.28)
project(kaepora VERSION 0.0.0 LANGUAGES C CXX) # x-release-please-version

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The code uses no C++ modules. Scanning for them would need clang-scan-deps, which Ubuntu packages apart from Clang.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Release builds keep assert on, so a broken invariant stops the program instead of writing a bad seed.
add_compile_options(-UNDEBUG)

if(WIN32)
    # Windows has no packages of these. EXCLUDE_FROM_ALL keeps their install rules out of the package.
    include(FetchContent)
    set(ZSTD_BUILD_SHARED OFF)
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_TESTS OFF)
    FetchContent_Declare(nlohmann_json EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
    FetchContent_Declare(spdlog EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744)
    FetchContent_Declare(zstd EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(nlohmann_json spdlog zstd)
    set(ZSTD_LIBRARY libzstd_static)
else()
    find_package(nlohmann_json REQUIRED)
    find_package(spdlog REQUIRED)
    find_package(zstd REQUIRED)
    # zstd before 1.5.6, as on Ubuntu 24.04, names only its shared and static targets.
    set(ZSTD_LIBRARY zstd::libzstd_shared)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(shipwright/CMake/soh-cvars.cmake)

# shipwright/ holds only the upstream files this build reads, so every source in it is compiled.
file(GLOB_RECURSE SHIPWRIGHT_SOURCES CONFIGURE_DEPENDS shipwright/*.c shipwright/*.cpp)

# Everything but the generator programs' main files, compiled once for all of them.
add_library(kaepora-generator OBJECT
    generator/cvars.cpp
    generator/game_state.cpp
    generator/generate.cpp
    generator/remake.cpp
    generator/seed_settings.cpp
    generator/setup.cpp
    $<IF:$<PLATFORM_ID:Windows>,generator/traps_windows.cpp,generator/traps.cpp>
    ${SHIPWRIGHT_SOURCES}
)
# configure_file rewrites version.h only when its text changes, so a new version rebuilds only its includers.
configure_file(version.h.in include/version.h @ONLY)
target_include_directories(kaepora-generator PUBLIC
    .
    ${PROJECT_BINARY_DIR}/include
    shipwright/soh/include
    shipwright/soh/src
    shipwright/soh/assets
    shipwright/soh
    shipwright/libultraship/include
)
# functions.h needs a level for game prints. 6 turns them off, as in SoH.
target_compile_definitions(kaepora-generator PUBLIC LOG_LEVEL_GAME_PRINTS=6)
# The item icon table drops const from texture names. The generator only reads the slot tables next to it.
set_source_files_properties(shipwright/soh/src/code/z_inventory.c PROPERTIES COMPILE_OPTIONS
    $<IF:$<C_COMPILER_ID:Clang>,-Wno-incompatible-pointer-types-discards-qualifiers,-Wno-discarded-qualifiers>)
# Clang takes #embed in C++ as an extension and warns at each use.
set_source_files_properties(generator/remake.cpp shipwright/soh/soh/Enhancements/Lang/Lang.cpp PROPERTIES
    COMPILE_OPTIONS $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)
target_link_libraries(kaepora-generator PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(kaepora-seedgen generator/main.cpp)
target_link_libraries(kaepora-seedgen PRIVATE kaepora-generator)

# Answers whether a location can be reached in a given world, for the logic tests.
add_executable(kaepora-reach generator/reach.cpp)
target_link_libraries(kaepora-reach PRIVATE kaepora-generator)

# Designated initializers leave out the members that start empty, which -Wextra would report. clang-cl reads -Wall as
# -Weverything, and its /W4 is Clang's -Wall -Wextra.
if(MSVC)
    set(WARNINGS /W4 -Wpedantic -Wno-missing-field-initializers)
else()
    set(WARNINGS -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

add_library(kaepora-patch-files STATIC patcher/bfevfl.cpp patcher/byml.cpp patcher/compression.cpp patcher/mod.cpp
                                       patcher/romfs.cpp)
target_include_directories(kaepora-patch-files PUBLIC patcher)
target_link_libraries(kaepora-patch-files PUBLIC nlohmann_json::nlohmann_json PRIVATE ${ZSTD_LIBRARY})
target_compile_options(kaepora-patch-files PRIVATE ${WARNINGS})

add_executable(kaepora-patch patcher/main.cpp)
target_link_libraries(kaepora-patch PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch PRIVATE ${WARNINGS})

add_executable(kaepora-patch-roundtrip tests/byml_roundtrip.cpp)
target_link_libraries(kaepora-patch-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-roundtrip PRIVATE ${WARNINGS})

add_executable(kaepora-patch-flow-roundtrip tests/bfevfl_roundtrip.cpp)
target_link_libraries(kaepora-patch-flow-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-flow-roundtrip PRIVATE ${WARNINGS})

# Makes seed folders with the window's MakeSeed, for the tests.
add_executable(kaepora-make-seed tests/make_seed.cpp kaepora/seed.cpp)
target_link_libraries(kaepora-make-seed PRIVATE kaepora-generator kaepora-patch-files)
target_compile_options(kaepora-make-seed PRIVATE ${WARNINGS})

# The window. Qt is found quietly, so everything else still builds where Qt is missing.
find_package(Qt6 QUIET COMPONENTS Widgets Network OPTIONAL_COMPONENTS Test)
if(Qt6_FOUND)
    add_executable(kaepora kaepora/main.cpp kaepora/new_version.cpp kaepora/seed.cpp kaepora/settings_dialog.cpp
                           kaepora/window.cpp $<$<PLATFORM_ID:Windows>:kaepora/kaepora.rc>)
    target_link_libraries(kaepora PRIVATE kaepora-generator kaepora-patch-files Qt6::Widgets Qt6::Network)
    target_compile_options(kaepora PRIVATE ${WARNINGS})
    set_target_properties(kaepora PROPERTIES WIN32_EXECUTABLE ON)

    # The window is the one program that installs, with the licences of the code built into it. On Windows it sits at
    # the top of the folder, beside the Visual C++ runtime, as Windows looks for a program's libraries in its own folder
    # first. tools/package-windows.sh adds Qt there.
    if(WIN32)
        set(LICENSES licenses)
        install(TARGETS kaepora DESTINATION .)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        include(InstallRequiredSystemLibraries)
        install(FILES ${nlohmann_json_SOURCE_DIR}/LICENSE.MIT DESTINATION ${LICENSES}/nlohmann_json)
        install(FILES ${spdlog_SOURCE_DIR}/LICENSE DESTINATION ${LICENSES}/spdlog)
        install(FILES ${spdlog_SOURCE_DIR}/include/spdlog/fmt/bundled/fmt.license.rst DESTINATION ${LICENSES}/fmt)
        install(FILES ${zstd_SOURCE_DIR}/LICENSE DESTINATION ${LICENSES}/zstd)
    else()
        include(GNUInstallDirs)
        set(LICENSES ${CMAKE_INSTALL_DATADIR}/licenses)
        install(TARGETS kaepora)
        install(FILES dist/io.github.pixelhabits.kaepora.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES dist/io.github.pixelhabits.kaepora.svg
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
        install(FILES dist/io.github.pixelhabits.kaepora.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    endif()
    install(FILES LICENSE DESTINATION ${LICENSES}/kaepora)
    install(FILES shipwright/libultraship/LICENSE DESTINATION ${LICENSES}/libultraship)
    install(FILES shipwright/soh/soh/Enhancements/randomizer/3drando/LICENSE.md DESTINATION ${LICENSES}/oot-randomizer)
endif()

enable_testing()
add_test(NAME upstream-tool COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/upstream_test.py)
foreach(TEST IN ITEMS only_shuffled_chests_change plan_lists_each_shuffled_chest plan_matches_spoiler
                     spoiler_log_lists_the_shuffled_locations_and_the_route
                     plan_opens_the_doors_the_rules_count_open plan_keeps_the_castle_quest_after_gohma
                     same_seed_same_files twenty_seeds_generate spirit_dungeon_items_stay_out_of_the_colossus
                     scrub_seeds_and_the_large_magic_jar_are_placed a_chest_kept_twice_holds_one_item
                     the_crate_heart_piece_and_the_training_ground_key_are_shuffled
                     spoiler_log_records_the_settings default_settings_are_the_settings_left_out
                     an_unknown_setting_is_refused a_dungeon_left_out_keeps_its_chests keys_stay_in_their_own_chests
                     maps_and_compasses_stay_in_their_own_chests lens_never_needed_leaves_the_lens_off_the_route
                     every_setting_changed_at_once strength_items_come_in_rising_order seed_text_ignores_case
                     fixed_seed_matches_fixture)
    add_test(NAME ${TEST} COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tests/seedgen_test.py
                                  $<TARGET_FILE:kaepora-seedgen> $<TARGET_FILE:kaepora-reach> ${PROJECT_VERSION}
                                  SeedgenTest.test_${TEST})
endforeach()
add_test(NAME logic COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tests/logic_test.py
                            $<TARGET_FILE:kaepora-reach>)

if(Qt6Test_FOUND)
    add_executable(kaepora-window-test tests/window_test.cpp kaepora/new_version.cpp kaepora/seed.cpp
                                       kaepora/settings_dialog.cpp kaepora/window.cpp)
    set_target_properties(kaepora-window-test PROPERTIES AUTOMOC ON)
    target_link_libraries(kaepora-window-test PRIVATE kaepora-generator kaepora-patch-files Qt6::Widgets Qt6::Network
                                                      Qt6::Test)
    target_compile_options(kaepora-window-test PRIVATE ${WARNINGS})
    # The window's tests that make no seed need no game file.
    foreach(TEST IN ITEMS lichiisModsFolderIsTheDefault aNewerVersionShowsTheNotice quietOutcomesShowNothing
                          theBoxTurnsTheCheckOff aDayPassesBetweenRequests aNewerVersionIsRememberedUntilItRuns
                          untickingStopsARequestInFlight)
        add_test(NAME window_${TEST} COMMAND kaepora-window-test ${TEST})
        # On Windows, QTest writes to the debugger rather than to stderr when no console is attached, as under ctest.
        set_tests_properties(window_${TEST} PROPERTIES ENVIRONMENT
                             "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
    endforeach()
endif()

# The game's container is Nintendo's data and stays out of git, so the tests that read it need its path.
set(KAEPORA_GAME_NUP "" CACHE FILEPATH "The game's .nup container, read by the patcher tests")
set(KAEPORA_TESTED_MOD "" CACHE FILEPATH "The placement file of the chest-swap mod tested in the game")
set(KAEPORA_TESTED_START_MOD "" CACHE FILEPATH "The game data list of the start-flag mod tested in the game")
set(KAEPORA_TESTED_SUBSTITUTE_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with one item in a chest's item and substitute values")
set(KAEPORA_TESTED_ADDED_ITEM_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with an item value added to two chests that named none")
set(KAEPORA_TESTED_GIFT_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the Boomerang in the potion shop child's bottle gift")
set(KAEPORA_TESTED_CASTLE_QUEST_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the warp after Gohma keeping the castle quest")
set(PATCH_TEST ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/patch_test.py)
if(KAEPORA_GAME_NUP)
    add_test(NAME patch-roundtrip COMMAND kaepora-patch-roundtrip "${KAEPORA_GAME_NUP}")
    add_test(NAME patch-flow-roundtrip COMMAND kaepora-patch-flow-roundtrip "${KAEPORA_GAME_NUP}")
    foreach(test larger-plan bad-input size-table size-table-unchanged together gift gift-size-table)
        add_test(NAME patch-${test} COMMAND ${PATCH_TEST} ${test} $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}")
    endforeach()
    add_test(NAME patch-fixed-seed-plan
        COMMAND ${PATCH_TEST} seed-plan $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                ${PROJECT_SOURCE_DIR}/tests/fixtures/plan-6332265119.json)
    foreach(TEST IN ITEMS same_files_as_the_two_programs two_seeds_in_one_process same_seed_twice_in_one_process
                          existing_folder_is_refused new_folder_in_a_missing_folder)
        add_test(NAME make_seed_${TEST}
                 COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/make_seed_test.py
                     $<TARGET_FILE:kaepora-make-seed> $<TARGET_FILE:kaepora-seedgen> $<TARGET_FILE:kaepora-patch>
                     "${KAEPORA_GAME_NUP}" MakeSeedTest.test_${TEST})
    endforeach()
    if(Qt6Test_FOUND)
        add_test(NAME window COMMAND kaepora-window-test)
        set_tests_properties(window PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;KAEPORA_GAME_NUP=${KAEPORA_GAME_NUP};KAEPORA_MAKE_SEED=$<TARGET_FILE:kaepora-make-seed>")
    endif()
    if(KAEPORA_TESTED_MOD)
        add_test(NAME patch-tested-mod
            COMMAND ${PATCH_TEST} tested-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}" "${KAEPORA_TESTED_MOD}")
    endif()
    if(KAEPORA_TESTED_START_MOD)
        add_test(NAME patch-tested-start-mod
            COMMAND ${PATCH_TEST} tested-start-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_START_MOD}")
    endif()
    if(KAEPORA_TESTED_SUBSTITUTE_MOD)
        add_test(NAME patch-tested-substitute-mod
            COMMAND ${PATCH_TEST} tested-substitute-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_SUBSTITUTE_MOD}")
    endif()
    if(KAEPORA_TESTED_ADDED_ITEM_MOD)
        add_test(NAME patch-tested-added-item-mod
            COMMAND ${PATCH_TEST} tested-added-item-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_ADDED_ITEM_MOD}")
    endif()
    if(KAEPORA_TESTED_GIFT_MOD)
        add_test(NAME patch-tested-gift-mod
            COMMAND ${PATCH_TEST} tested-gift-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_GIFT_MOD}")
    endif()
    if(KAEPORA_TESTED_CASTLE_QUEST_MOD)
        add_test(NAME patch-tested-castle-quest-mod
            COMMAND ${PATCH_TEST} tested-castle-quest-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_CASTLE_QUEST_MOD}")
    endif()
endif()

# Windows finds a program's libraries beside it or on PATH, so the tests find Qt's on PATH.
if(WIN32 AND Qt6Test_FOUND)
    get_property(tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${tests} PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
