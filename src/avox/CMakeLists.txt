# avox core: public interfaces live at src/avox root (avc-style),
# implementations arrive in subdirectories (player/ source/ gpu/ ...).
add_library(avox_core INTERFACE)
add_library(avox::core ALIAS avox_core)

# include root is src/ so consumers write #include <avox/AvoxPlayer.h>
target_include_directories(avox_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<INSTALL_INTERFACE:include>)

target_compile_features(avox_core INTERFACE cxx_std_20)
