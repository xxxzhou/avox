# avox_core: header-only public interface layer for now.
# Implementation modules (player, gpu pipeline, sources) attach here as they land.
add_library(avox_core INTERFACE)
add_library(avox::core ALIAS avox_core)

target_include_directories(avox_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_compile_features(avox_core INTERFACE cxx_std_20)
