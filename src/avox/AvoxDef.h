#pragma once
// Basic macros: namespace gate, export, shared-build switch.

#if defined(_WIN32) && defined(AVOX_BUILD_SHARED)
  #ifdef AVOX_EXPORTING
    #define AVOX_EXPORT __declspec(dllexport)
  #else
    #define AVOX_EXPORT __declspec(dllimport)
  #endif
#else
  #define AVOX_EXPORT
#endif
