cmake_minimum_required(VERSION 3.28)
project(pinsel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(SANITIZE AND WIN32)
  # AddressSanitizer on Windows needs the release C runtime. UndefinedBehaviorSanitizer
  # runs in trap mode there: it crashes on the first fault and needs no runtime library.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  add_compile_options(-fsanitize=address,undefined -fsanitize-trap=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Warnings as errors for this project's targets only, not for GoogleTest.
set(WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

add_library(pinsel-lib src/project.cpp)
target_include_directories(pinsel-lib PUBLIC src)
target_compile_options(pinsel-lib PRIVATE ${WARNINGS})

add_executable(pinsel src/main.cpp)
target_link_libraries(pinsel PRIVATE pinsel-lib)
target_compile_options(pinsel PRIVATE ${WARNINGS})

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
endif()
