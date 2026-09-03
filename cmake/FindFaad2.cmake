# 查找FAAD2的头文件目录
find_path(FAAD2_INCLUDE_DIR
    NAMES faad.h
    PATHS ${AVOX_MOEDULE_BUILD_DIR}/faad2/include)
set(FAAD2_INCLUDE_DIRS ${FAAD2_INCLUDE_DIR})

# 设置FAAD2库文件所在目录
if(WIN32)
    set(FAAD2_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/faad2/${CMAKE_BUILD_TYPE}) 
elseif(IOS)
    set(FAAD2_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/faad2/${CMAKE_BUILD_TYPE}-iphoneos)        
elseif(ANDROID OR UNIX)
    set(FAAD2_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/faad2) 
endif()

message(STATUS "FIND FAAD2_LIB_DIR: ${FAAD2_LIB_DIR}")
# 查找FAAD2库和mk_api库
find_library(FAAD2_LIBRARY
    NAMES faad
    NAMES_PER_DIR 
    PATHS ${FAAD2_LIB_DIR}
    PATH_SUFFIXES lib 
    NO_DEFAULT_PATH 
    )
set(FAAD2_LIBRARYS ${FAAD2_LIBRARY})  
message(STATUS "FAAD2_LIBRARY: ${FAAD2_LIBRARY}")
# Faad2_FOUND变量
include(FindPackageHandleStandardArgs)
# 需要注意FAAD2和文件FindFAAD2.cmake要一致，大小写一致
find_package_handle_standard_args(FAAD2 DEFAULT_MSG FAAD2_LIBRARYS FAAD2_INCLUDE_DIRS)

if(FAAD2_FOUND)
    # 在Windows平台下，如果找到Faad2库
    if(WIN32)
        # 查找mk_api.dll文件
        find_file(FAAD2_API_DLLS
            NAMES faad-2.dll
            PATHS ${FAAD2_LIB_DIR})             
        avox_run_module_copy("${FAAD2_API_DLLS}")
    else()
        avox_run_module_copy("${FAAD2_LIBRARY}")
    endif()
endif()


