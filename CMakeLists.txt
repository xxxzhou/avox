cmake_minimum_required(VERSION 3.24)

project(avox
  VERSION 0.1.0
  DESCRIPTION "avox - cross-platform media framework (capture / GPU pipeline / player / record)"
  HOMEPAGE_URL "https://github.com/xxxzhou/avox"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVOX_BUILD_TESTS    "Build avox tests"                      ON)
option(AVOX_ENABLE_VULKAN  "Build the Vulkan GPU pipeline module"  OFF)
option(AVOX_ENABLE_PLUGINS "Build bundled source plugins"          OFF)

add_subdirectory(core)

if(AVOX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
