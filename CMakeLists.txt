cmake_minimum_required(VERSION 3.28)
# vcpkg's default Windows triplet builds DLLs and copies the debug ones beside a
# Debug build, but the sanitizer build below runs on the release C runtime. Static
# libraries on the release runtime DLL avoid mixing the two.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
endif()
set(VCPKG_OVERLAY_TRIPLETS ${CMAKE_CURRENT_SOURCE_DIR}/triplets)
project(pinsel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PINSEL_TRACY "Send profiling data to the Tracy profiler" OFF)
if(SANITIZE AND WIN32)
  # AddressSanitizer on Windows needs the release C runtime. UndefinedBehaviorSanitizer
  # runs in trap mode there: it crashes on the first fault and needs no runtime library.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  # Qt's debug DLLs use the debug C runtime, so link its release DLLs.
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
  add_compile_options(-fsanitize=address,undefined -fsanitize-trap=undefined -fno-omit-frame-pointer)
  # Under AddressSanitizer the MSVC STL marks its containers in each object, and the
  # linker refuses to mix them with vcpkg's libraries, which build without it.
  add_compile_definitions(_DISABLE_STL_ANNOTATION)
  add_link_options(-fsanitize=address,undefined)
elseif(SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Svg is never linked, but its icon plugin draws the tool icons; without it they stay blank.
# QRhi, which the canvas draws with, is in Qt's private Gui module, which warns on use.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Svg Concurrent GuiPrivate ShaderTools)
qt_standard_project_setup()
find_package(benchmark CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# Warnings as errors for this project's targets only, not for GoogleTest.
set(WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

add_library(pinsel-lib src/app/project.cpp)
target_include_directories(pinsel-lib PUBLIC src)
target_compile_options(pinsel-lib PRIVATE ${WARNINGS})

add_library(pinsel-tiles src/tiles/tile_store.cpp src/tiles/image_file.cpp src/tiles/openraster.cpp
                         src/tiles/zip.cpp)
target_include_directories(pinsel-tiles PUBLIC src)
target_link_libraries(pinsel-tiles PUBLIC Qt6::Gui PRIVATE ZLIB::ZLIB)
target_compile_options(pinsel-tiles PRIVATE ${WARNINGS})

add_library(pinsel-brush src/brush/brush.cpp src/brush/lasso.cpp)
target_link_libraries(pinsel-brush PUBLIC pinsel-tiles)
target_compile_options(pinsel-brush PRIVATE ${WARNINGS})

# The engine runs models in another process. Its Python sidecar stays out of
# the build: uv starts it from the source tree.
add_library(pinsel-engine src/engine/engine.cpp)
target_include_directories(pinsel-engine PUBLIC src)
target_link_libraries(pinsel-engine PUBLIC Qt6::Core)
target_compile_definitions(pinsel-engine PRIVATE PINSEL_SIDECAR_DIR="${CMAKE_SOURCE_DIR}/sidecar")
target_compile_options(pinsel-engine PRIVATE ${WARNINGS})

# Crops the mask's region for the model and lays the answer back as a layer.
add_library(pinsel-inpaint src/inpaint/inpaint.cpp)
target_link_libraries(pinsel-inpaint PUBLIC pinsel-tiles)
target_compile_options(pinsel-inpaint PRIVATE ${WARNINGS})

add_library(pinsel-canvas src/canvas/canvas.cpp src/canvas/view.cpp src/canvas/gpu/textures.cpp
                          src/canvas/gpu/frame.cpp)
target_include_directories(pinsel-canvas PUBLIC src)
target_link_libraries(pinsel-canvas PUBLIC Qt6::Widgets pinsel-brush PRIVATE Qt6::GuiPrivate)
qt_add_shaders(pinsel-canvas shaders PREFIX /shaders BASE src/canvas/shaders
               FILES src/canvas/shaders/tile.vert src/canvas/shaders/tile.frag
                     src/canvas/shaders/quad.vert src/canvas/shaders/blend.frag
                     src/canvas/shaders/present.frag)
# The Tracy client defines TRACY_ENABLE for everything that links it. Without it,
# Tracy's header turns every macro into nothing, so code can mark zones in every build.
if(PINSEL_TRACY)
  target_link_libraries(pinsel-canvas PUBLIC Tracy::TracyClient)
else()
  target_include_directories(
    pinsel-canvas SYSTEM PUBLIC $<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_options(pinsel-canvas PRIVATE ${WARNINGS})

qt_add_executable(pinsel WIN32 src/app/main.cpp src/ui/main_window.cpp src/ui/dialogs/new_image_dialog.cpp
                  src/ui/theme.cpp src/ui/layers/layers_panel.cpp
                  src/ui/engine/engine_status.cpp src/ui/engine/prompt_panel.cpp)
file(GLOB ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(pinsel icons PREFIX /icons BASE resources/icons FILES ${ICONS})
target_link_libraries(pinsel PRIVATE pinsel-lib pinsel-canvas pinsel-engine pinsel-inpaint
                                     Qt6::Concurrent)
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

  # CI posts every PNG the tests write here to the pull request.
  set(SHOTS ${CMAKE_BINARY_DIR}/screenshots)
  file(MAKE_DIRECTORY ${SHOTS})

  add_executable(tests tests/test_brush.cpp tests/canvas/test_canvas.cpp tests/test_memory.cpp
                       tests/test_project.cpp tests/canvas/test_view.cpp tests/tiles/test_tile_store.cpp
                       tests/tiles/test_image_file.cpp tests/engine/test_engine.cpp
                       tests/engine/test_inpaint.cpp)
  target_link_libraries(tests PRIVATE pinsel-lib pinsel-canvas pinsel-brush pinsel-tiles
                                      pinsel-engine pinsel-inpaint GTest::gtest_main)
  target_compile_options(tests PRIVATE ${WARNINGS})
  target_compile_definitions(tests PRIVATE PINSEL_SCREENSHOTS="${SHOTS}"
                                           PINSEL_FAKE_ENGINE="$<TARGET_FILE:fake-engine>")

  # Stands in for the Python sidecar, so the tests need no Python.
  add_executable(fake-engine tests/engine/fake_engine.cpp)
  target_link_libraries(fake-engine PRIVATE Qt6::Gui)
  target_compile_options(fake-engine PRIVATE ${WARNINGS})
  add_dependencies(tests fake-engine)
  # The load peak test reads the heap through AddressSanitizer's allocator.
  if(SANITIZE)
    target_compile_definitions(tests PRIVATE PINSEL_ASAN)
  endif()
  include(GoogleTest)
  # The canvas tests make a QApplication; listing the tests makes none.
  if(WIN32)
    gtest_discover_tests(tests)
  else()
    gtest_discover_tests(tests PROPERTIES ENVIRONMENT_MODIFICATION QT_QPA_PLATFORM=set:offscreen)
  endif()

  # One pass of each benchmark, so the harness keeps working under the sanitizers.
  add_test(NAME bench_smoke COMMAND bench --benchmark_min_time=1x)

  # A 1080p screen, and a 4K screen at 200% scaling. The image spans several
  # tiles, so a seam would show.
  set(PATTERN ${CMAKE_SOURCE_DIR}/tests/data/pattern.png)
  add_test(NAME screenshot_1080p
           COMMAND pinsel --screenshot ${SHOTS}/main-window-1080p.png --size 1920x1080
                   --open ${PATTERN})
  add_test(NAME screenshot_layers
           COMMAND pinsel --screenshot ${SHOTS}/layers-panel.png --size 1280x800
                   --new 1024x768 --layers 3)
  add_test(NAME screenshot_4k
           COMMAND pinsel --screenshot ${SHOTS}/main-window-4k.png --size 1920x1080
                   --open ${PATTERN})
  # Layers with each blend mode, a hidden one and a mask, read from OpenRaster.
  add_test(NAME screenshot_openraster
           COMMAND pinsel --screenshot ${SHOTS}/openraster-1080p.png --size 1920x1080
                   --open ${CMAKE_SOURCE_DIR}/tests/data/layers.ora)
  # A new blank image, with no file behind it.
  add_test(NAME screenshot_new
           COMMAND pinsel --screenshot ${SHOTS}/new-image-1080p.png --size 1920x1080
                   --new 1024x1024)
  # A star filled into the mask with the lasso; its arms overlap and still fill.
  add_test(NAME screenshot_lasso
           COMMAND pinsel --screenshot ${SHOTS}/lasso-mask-1080p.png --size 1920x1080
                   --new 1024x768 --lasso "512,104 620,560 260,280 764,280 404,560")
  # The status bar after a model loads, and after the engine crashes as it loads.
  add_test(NAME screenshot_engine_loaded
           COMMAND pinsel --screenshot ${SHOTS}/engine-loaded-1080p.png --size 1920x1080
                   --new 1024x768 --load-model sdxl.safetensors)
  add_test(NAME screenshot_engine_stopped
           COMMAND pinsel --screenshot ${SHOTS}/engine-stopped-1080p.png --size 1920x1080
                   --new 1024x768 --load-model crash)
  # A mask on the pattern inpainted by the fake engine, which inverts the masked
  # part: the result layer sits in the panel above the image.
  add_test(NAME screenshot_inpaint
           COMMAND pinsel --screenshot ${SHOTS}/inpaint-1080p.png --size 1920x1080
                   --open ${PATTERN} --lasso "300,200 700,180 760,560 340,600"
                   --load-model sdxl.safetensors --inpaint "a red fox")
  # Pin the scale so the screen's own DPI never changes the size. Help goes to the
  # console, not a message box that would hang the test on Windows.
  set(SCREENSHOT_TESTS screenshot_1080p screenshot_4k screenshot_new screenshot_layers
                       screenshot_openraster screenshot_lasso screenshot_engine_loaded
                       screenshot_engine_stopped screenshot_inpaint)
  set_property(TEST ${SCREENSHOT_TESTS} PROPERTY ENVIRONMENT_MODIFICATION
               QT_ENABLE_HIGHDPI_SCALING=set:0 QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=set:1)
  set_property(TEST screenshot_1080p screenshot_new screenshot_layers screenshot_openraster
               screenshot_lasso screenshot_engine_loaded screenshot_engine_stopped
               screenshot_inpaint APPEND
               PROPERTY ENVIRONMENT_MODIFICATION QT_SCALE_FACTOR=set:1)
  set_property(TEST screenshot_4k APPEND PROPERTY ENVIRONMENT_MODIFICATION QT_SCALE_FACTOR=set:2)
  set_property(TEST screenshot_engine_loaded screenshot_engine_stopped screenshot_inpaint
               APPEND PROPERTY ENVIRONMENT_MODIFICATION
               PINSEL_ENGINE=set:$<TARGET_FILE:fake-engine>)
  # Windows draws with its native platform to show the real style; offscreen has
  # no fonts there.
  if(NOT WIN32)
    set_property(TEST ${SCREENSHOT_TESTS} bench_smoke APPEND
                 PROPERTY ENVIRONMENT_MODIFICATION QT_QPA_PLATFORM=set:offscreen)
  endif()
endif()
