#pragma once

#include <stdint.h>

// 关闭window平台的自定义的相关min/max
#ifndef NOMINMAX
#define NOMINMAX 1
#endif


// nlohmann json不同平台的宏定义
// https://github.com/nlohmann/json/blob/develop/include/nlohmann/json.hpp

// _WIN32：用于 Windows 平台。
// __ANDROID__：用于 Android 平台。
// __APPLE__：用于苹果相关平台（包括 iOS 和 macOS），进一步细分：
    // __IPHONEOS__：用于 iOS 平台。
    // __MACOS__：用于 macOS 平台。
// __LINUX__：用于 Linux 系统。
// __UNIX__：用于 Unix - like 系统（包括 Linux、macOS 等，但不一定包括 Windows）。
// Emscripten  

// 导出符号,原则上给外部项目使用只有结构以及抽象类 .h的头文件
// 只在当前项目各模块内使用导出带AVOX_EXPORT有实际实现的C++类
#ifdef _WIN32
    #if defined(AVOX_EXPORT_DEFINE)
        #define AVOX_EXPORT __declspec(dllexport)
    #else
        #define AVOX_EXPORT __declspec(dllimport)
    #endif  
#elif defined(__ANDROID__) || defined(__APPLE__) || defined(__LINUX__) || defined(__UNIX__)
    #if defined(AVOX_EXPORT_DEFINE)
        #define AVOX_EXPORT __attribute__((visibility("default")))
    #else
        #define AVOX_EXPORT
    #endif
#else
    #define AVOX_EXPORT
#endif

// https://www.hellobit.com.cn/b/767368973/2826001615.html
// __EMSCRIPTEN__

// wasm 非win32/linux同层，组合关系，但是在AVOX_EXPORT定义类似同层
#if defined(__EMSCRIPTEN__)
    // 导入Emscripten相关头文件，用于获取EMSCRIPTEN_KEEPALIVE宏定义
    #include <emscripten/emscripten.h>
    // 使用正确的Emscripten宏来标记函数可导出且在模块初始化后不被优化掉
    #define AVOX_EXPORT EMSCRIPTEN_KEEPALIVE
#endif

// 检测是否支持 C++17 的宏
#if __cplusplus >= 201703L
    #define AVOX_HAS_CPP17 1
#else
    #define AVOX_HAS_CPP17 0
#endif

#define AVOX_TSTR(x) #x

// ============== OpenCV 兼容深度/通道类型描述 ==============
// 采用opencv里的数据描述,前三BIT表示类型,后三BIT表示通道个数
#define AVOX_CV_CN_MAX 512
#define AVOX_CV_CN_SHIFT 3
#define AVOX_CV_DEPTH_MAX (1 << AVOX_CV_CN_SHIFT)

#define AVOX_CV_8U 0
#define AVOX_CV_8S 1
#define AVOX_CV_16U 2
#define AVOX_CV_16S 3
#define AVOX_CV_32S 4
#define AVOX_CV_32F 5
#define AVOX_CV_64F 6
#define AVOX_CV_16F 7

#define AVOX_CV_MAT_DEPTH_MASK (AVOX_CV_DEPTH_MAX - 1)
#define AVOX_CV_MAT_DEPTH(flags) ((flags)&AVOX_CV_MAT_DEPTH_MASK)

#define AVOX_CV_MAKETYPE(depth, cn) \
  (AVOX_CV_MAT_DEPTH(depth) + (((cn)-1) << AVOX_CV_CN_SHIFT))
#define AVOX_CV_MAKE_TYPE AVOX_CV_MAKETYPE

#define AVOX_CV_8UC1 AVOX_CV_MAKETYPE(AVOX_CV_8U, 1)
#define AVOX_CV_8UC2 AVOX_CV_MAKETYPE(AVOX_CV_8U, 2)
#define AVOX_CV_8UC3 AVOX_CV_MAKETYPE(AVOX_CV_8U, 3)
#define AVOX_CV_8UC4 AVOX_CV_MAKETYPE(AVOX_CV_8U, 4)
#define AVOX_CV_8UC(n) AVOX_CV_MAKETYPE(AVOX_CV_8U, (n))

#define AVOX_CV_8SC1 AVOX_CV_MAKETYPE(AVOX_CV_8S, 1)
#define AVOX_CV_8SC2 AVOX_CV_MAKETYPE(AVOX_CV_8S, 2)
#define AVOX_CV_8SC3 AVOX_CV_MAKETYPE(AVOX_CV_8S, 3)
#define AVOX_CV_8SC4 AVOX_CV_MAKETYPE(AVOX_CV_8S, 4)
#define AVOX_CV_8SC(n) AVOX_CV_MAKETYPE(AVOX_CV_8S, (n))

#define AVOX_CV_16UC1 AVOX_CV_MAKETYPE(AVOX_CV_16U, 1)
#define AVOX_CV_16UC2 AVOX_CV_MAKETYPE(AVOX_CV_16U, 2)
#define AVOX_CV_16UC3 AVOX_CV_MAKETYPE(AVOX_CV_16U, 3)
#define AVOX_CV_16UC4 AVOX_CV_MAKETYPE(AVOX_CV_16U, 4)
#define AVOX_CV_16UC(n) AVOX_CV_MAKETYPE(AVOX_CV_16U, (n))

#define AVOX_CV_16SC1 AVOX_CV_MAKETYPE(AVOX_CV_16S, 1)
#define AVOX_CV_16SC2 AVOX_CV_MAKETYPE(AVOX_CV_16S, 2)
#define AVOX_CV_16SC3 AVOX_CV_MAKETYPE(AVOX_CV_16S, 3)
#define AVOX_CV_16SC4 AVOX_CV_MAKETYPE(AVOX_CV_16S, 4)
#define AVOX_CV_16SC(n) AVOX_CV_MAKETYPE(AVOX_CV_16S, (n))

#define AVOX_CV_32SC1 AVOX_CV_MAKETYPE(AVOX_CV_32S, 1)
#define AVOX_CV_32SC2 AVOX_CV_MAKETYPE(AVOX_CV_32S, 2)
#define AVOX_CV_32SC3 AVOX_CV_MAKETYPE(AVOX_CV_32S, 3)
#define AVOX_CV_32SC4 AVOX_CV_MAKETYPE(AVOX_CV_32S, 4)
#define AVOX_CV_32SC(n) AVOX_CV_MAKETYPE(AVOX_CV_32S, (n))

#define AVOX_CV_32FC1 AVOX_CV_MAKETYPE(AVOX_CV_32F, 1)
#define AVOX_CV_32FC2 AVOX_CV_MAKETYPE(AVOX_CV_32F, 2)
#define AVOX_CV_32FC3 AVOX_CV_MAKETYPE(AVOX_CV_32F, 3)
#define AVOX_CV_32FC4 AVOX_CV_MAKETYPE(AVOX_CV_32F, 4)
#define AVOX_CV_32FC(n) AVOX_CV_MAKETYPE(AVOX_CV_32F, (n))

#define AVOX_CV_64FC1 AVOX_CV_MAKETYPE(AVOX_CV_64F, 1)
#define AVOX_CV_64FC2 AVOX_CV_MAKETYPE(AVOX_CV_64F, 2)
#define AVOX_CV_64FC3 AVOX_CV_MAKETYPE(AVOX_CV_64F, 3)
#define AVOX_CV_64FC4 AVOX_CV_MAKETYPE(AVOX_CV_64F, 4)
#define AVOX_CV_64FC(n) AVOX_CV_MAKETYPE(AVOX_CV_64F, (n))

#define AVOX_CV_16FC1 AVOX_CV_MAKETYPE(AVOX_CV_16F, 1)
#define AVOX_CV_16FC2 AVOX_CV_MAKETYPE(AVOX_CV_16F, 2)
#define AVOX_CV_16FC3 AVOX_CV_MAKETYPE(AVOX_CV_16F, 3)
#define AVOX_CV_16FC4 AVOX_CV_MAKETYPE(AVOX_CV_16F, 4)
#define AVOX_CV_16FC(n) AVOX_CV_MAKETYPE(AVOX_CV_16F, (n))

#define AVOX_CV_MAT_CN_MASK ((AVOX_CV_CN_MAX - 1) << AVOX_CV_CN_SHIFT)
#define AVOX_CV_MAT_CN(flags) \
  ((((flags)&AVOX_CV_MAT_CN_MASK) >> AVOX_CV_CN_SHIFT) + 1)
/** Size of each channel item,
   0x28442211 = 0010 1000 0100 0100 0010 0010 0001 0001 ~ array of
   sizeof(arr_type_elem) */
#define AVOX_CV_ELEM_SIZE1(type) \
  ((0x28442211 >> AVOX_CV_MAT_DEPTH(type) * 4) & 15)
#define AVOX_CV_ELEM_SIZE(type) \
  (AVOX_CV_MAT_CN(type) * AVOX_CV_ELEM_SIZE1(type))