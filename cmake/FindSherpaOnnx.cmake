# 查找 Sherpa-onnx 的头文件目录
find_path(SHERPA_INCLUDE_DIR
    NAMES sherpa-onnx/c-api/c-api.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/sherpa-onnx)
set(SHERPA_INCLUDE_DIRS ${SHERPA_INCLUDE_DIR})
message(STATUS "SHERPA_INCLUDE_DIRS: ${SHERPA_INCLUDE_DIRS}")

# 查找 ONNX Runtime (sherpa-onnx 依赖)
find_package(ONNX)

# 设置 SHERPA 库文件所在目录
if(WIN32)
    # Windows 动态库模式
    set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/lib/${CMAKE_BUILD_TYPE})
    set(SHERPA_DLL_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/bin/${CMAKE_BUILD_TYPE})
elseif(IOS)
    set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/${CMAKE_BUILD_TYPE}-iphoneos)
elseif(ANDROID)
    set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/lib)
elseif(APPLE)
    # macOS(Xcode 多配置生成器): 库落在 sherpa-onnx/lib/<Config>/, 兼容顶层 <Config>/ 与 lib/
    set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/lib/${CMAKE_BUILD_TYPE})
    if(NOT EXISTS "${SHERPA_LIB_DIR}")
        set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/${CMAKE_BUILD_TYPE})
    endif()
    if(NOT EXISTS "${SHERPA_LIB_DIR}")
        set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/lib)
    endif()
elseif(UNIX)
    set(SHERPA_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sherpa-onnx/lib)
endif()

message(STATUS "FIND SHERPA_LIB_DIR: ${SHERPA_LIB_DIR}")

# 查找所有 SHERPA 库（静态库模式）
if(WIN32)
    file(GLOB SHERPA_ALL_LIBS "${SHERPA_LIB_DIR}/*.lib")
else()
    file(GLOB SHERPA_ALL_LIBS "${SHERPA_LIB_DIR}/*.a")
endif()

# Android / macOS 需要额外链接 onnxruntime: sherpa 静态库对 ORT 是未定义符号
# (nm -u libsherpa-onnx-core.a 里有 _OrtGetApiBase), 与 avox_onnx 插件共用同一份运行时
if((ANDROID OR APPLE) AND ONNX_FOUND AND SHERPA_ALL_LIBS)
    set(SHERPA_LIBRARYS ${SHERPA_ALL_LIBS} ${ONNXRUNTIME_LIBRARIES})
    message(STATUS "Added onnxruntime library: ${ONNXRUNTIME_LIBRARIES}")
else()
    set(SHERPA_LIBRARYS ${SHERPA_ALL_LIBS})
endif()

message(STATUS "SHERPA_LIBRARYS: ${SHERPA_LIBRARYS}")

# SHERPA_FOUND 变量
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SherpaOnnx DEFAULT_MSG SHERPA_LIBRARYS SHERPA_INCLUDE_DIRS)

if(SherpaOnnx_FOUND)
    if(WIN32)
        # Windows: 查找 DLL (供 avox_sherpa 插件 DEP_DLLS 拷进 plugins/, 使 plugins/ 自包含)
        # 不再 avox_run_module_copy 到顶层 —— sherpa-onnx-c-api.dll 只随插件进 plugins/
        message(STATUS "SHERPA DLL DIR: ${SHERPA_DLL_DIR}")
        file(GLOB SHERPA_DLLS "${SHERPA_DLL_DIR}/*.dll")
        # onnxruntime.dll 统一由 avox_onnx 的 DML drop 提供(FindONNX), 此处排除
        # 避免 CPU 版把 DML 版覆盖回去; 同为 1.23.2, C API 对 sherpa 向后兼容。
        list(FILTER SHERPA_DLLS EXCLUDE REGEX "onnxruntime[^/]*\\.dll$")
        message(STATUS "SHERPA_DLLS found: ${SHERPA_DLLS}")
    endif()
endif()
