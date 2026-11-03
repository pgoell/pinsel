cmake_minimum_required(VERSION 3.28)
# vcpkg's default Windows triplet builds DLLs and copies the debug ones beside a
# Debug build, but the sanitizer build below runs on the release C runtime. Static
# libraries on the release runtime DLL avoid mixing the two.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
endif()
project(pinsel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(SANITIZE AND WIN32)
  # AddressSanitizer on Windows needs the release C runtime. UndefinedBehaviorSanitizer
  # runs in trap mode there: it crashes on the first fault and needs no runtime library.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  # Qt's debug DLLs use the debug C runtime, so link its release DLLs.
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
  add_compile_options(-fsanitize=address,undefined -fsanitize-trap=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Svg is never linked, but its icon plugin draws the tool icons; without it they stay blank.
find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Svg)
qt_standard_project_setup()
find_package(benchmark CONFIG REQUIRED)

# Warnings as errors for this project's targets only, not for GoogleTest.
set(WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

add_library(pinsel-lib src/project.cpp)
target_include_directories(pinsel-lib PUBLIC src)
target_compile_options(pinsel-lib PRIVATE ${WARNINGS})

add_library(pinsel-canvas src/canvas/canvas.cpp)
target_include_directories(pinsel-canvas PUBLIC src)
target_link_libraries(pinsel-canvas PUBLIC Qt6::Widgets)
target_compile_options(pinsel-canvas PRIVATE ${WARNINGS})

qt_add_executable(pinsel WIN32 src/main.cpp src/ui/main_window.cpp src/ui/theme.cpp)
file(GLOB ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(pinsel icons PREFIX /icons BASE resources/icons FILES ${ICONS})
target_link_libraries(pinsel PRIVATE pinsel-lib pinsel-canvas)
target_compile_options(pinsel PRIVATE ${WARNINGS})

add_executable(bench bench/pan_zoom.cpp)
target_link_libraries(bench PRIVATE pinsel-canvas benchmark::benchmark)
target_compile_options(bench PRIVATE ${WARNINGS})

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  # Pinned by hash. Dependabot does not read CMake: bump it by hand.
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  # Use the same C runtime as pinsel on Windows.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(tests tests/test_project.cpp)
  target_link_libraries(tests PRIVATE pinsel-lib GTest::gtest_main)
  target_compile_options(tests PRIVATE ${WARNINGS})
  include(GoogleTest)
  gtest_discover_tests(tests)

  # One pass of each benchmark, so the harness keeps working under the sanitizers.
  add_test(NAME bench_smoke COMMAND bench --benchmark_min_time=1x)

  # CI posts every PNG these write to the pull request: a 1080p screen, and a 4K
  # screen at 200% scaling.
  set(SHOTS ${CMAKE_BINARY_DIR}/screenshots)
  file(MAKE_DIRECTORY ${SHOTS})
  add_test(NAME screenshot_1080p
           COMMAND pinsel --screenshot ${SHOTS}/main-window-1080p.png --size 1920x1080)
  add_test(NAME screenshot_4k
           COMMAND pinsel --screenshot ${SHOTS}/main-window-4k.png --size 1920x1080)
  # Pin the scale so the screen's own DPI never changes the size. Help goes to the
  # console, not a message box that would hang the test on Windows.
  set_property(TEST screenshot_1080p screenshot_4k PROPERTY ENVIRONMENT_MODIFICATION
               QT_ENABLE_HIGHDPI_SCALING=set:0 QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=set:1)
  set_property(TEST screenshot_1080p APPEND PROPERTY ENVIRONMENT_MODIFICATION QT_SCALE_FACTOR=set:1)
  set_property(TEST screenshot_4k APPEND PROPERTY ENVIRONMENT_MODIFICATION QT_SCALE_FACTOR=set:2)
  # Windows draws with its native platform to show the real style; offscreen has
  # no fonts there.
  if(NOT WIN32)
    set_property(TEST screenshot_1080p screenshot_4k bench_smoke APPEND
                 PROPERTY ENVIRONMENT_MODIFICATION QT_QPA_PLATFORM=set:offscreen)
  endif()
endif()
