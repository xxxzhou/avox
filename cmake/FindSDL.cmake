# 查找SDL的头文件目录
find_path(SDL_INCLUDE_DIR
    NAMES SDL3/SDL.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/SDL/include
    NO_SYSTEM_ENVIRONMENT_PATH  # 禁止搜索系统路径
    NO_CMAKE_SYSTEM_PATH         # 禁止搜索系统级CMake路径
    )
set(SDL_INCLUDE_DIRS ${SDL_INCLUDE_DIR})

# 设置SDL库文件所在目录
if(WIN32)
    set(SDL_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/SDL/${CMAKE_BUILD_TYPE}) 
elseif(IOS) 
    set(SDL_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/SDL/${CMAKE_BUILD_TYPE}-iphoneos)        
elseif(ANDROID OR UNIX)
    set(SDL_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/SDL)     
endif()
message(STATUS "SDL_LIB_DIR: ${SDL_LIB_DIR}") 
# 查找SDL库
find_library(SDL_LIBRARY
    NAMES SDL3
    PATHS ${SDL_LIB_DIR})
set(SDL_LIBRARYS ${SDL_LIBRARY})  

# SDL_FOUND变量
include(FindPackageHandleStandardArgs)
# 需要注意SDL和文件FindSDL.cmake要一致，大小写一致
find_package_handle_standard_args(SDL DEFAULT_MSG SDL_LIBRARYS SDL_INCLUDE_DIRS)

if(SDL_FOUND)
    # 在Windows平台下，如果找到SDL库
    if(WIN32)
        # 查找mk_api.dll文件
        find_file(SDL_API_DLLS
            NAMES SDL3.dll
            PATHS ${SDL_LIB_DIR})
        avox_run_module_copy("${SDL_API_DLLS}")
    else()
        avox_run_module_copy("${SDL_LIBRARYS}")
    endif()
endif()


