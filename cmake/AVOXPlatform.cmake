# WIN32/UNIX/APPLE/__IPHONEOS__/__MACOSX__
if(UNIX)
  # UNIX/Linux/Darwin
  set(COMPILE_OPTIONS_DEFAULT
    "-fPIC"
    "-Wall;-Wextra"
    "-Wno-unused-function;-Wno-unused-parameter;-Wno-unused-variable;-Wno-deprecated-declarations"
    "-Wno-error=extra;-Wno-error=missing-field-initializers;-Wno-error=type-limits;-Wno-comment")
  if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(COMPILE_OPTIONS_DEFAULT ${COMPILE_OPTIONS_DEFAULT} "-g3")
  else()
    set(COMPILE_OPTIONS_DEFAULT ${COMPILE_OPTIONS_DEFAULT} "-g0")
  endif()
elseif(WIN32)
  if (MSVC)
    set(COMPILE_OPTIONS_DEFAULT
            # TODO: /wd4819 应该是不会生效
            "/wd4566;/wd4819;/utf-8"
            # warning C4530: C++ exception handler used, but unwind semantics are not enabled.
            "/EHsc")
    # disable Windows logo
    list(APPEND COMPILE_OPTIONS_DEFAULT "/nologo" "/MP")
    list(APPEND CMAKE_STATIC_LINKER_FLAGS "/nologo")
  else()
    # widnows使用clang可能不正常识别WIN32编译符
    add_definitions(-DWIN32)
  endif()
endif()

if(IOS) 
  # Add iOS-specific compiler flags to suppress WebRTC nullability warnings
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-nullability-completeness -Wno-nullability-extension")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nullability-completeness -Wno-nullability-extension")
endif()

# 16K对齐
if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
endif()

# 交叉编译，windows编译android这种
# CMAKE_CROSSCOMPILING

# Android 16K page size support (required for Android 15+)
# if(ANDROID)
#   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
#   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
# endif()
