# Android

## 移植到android

在windows版本完成基本功能下，开始考虑移植到android。首先要解决第三方库到android下的编译问题。

针对编译准备改进：

1. 所有bulid_project.py脚本，都引用build_common.py，build_common.py中包含所有编译的通用设置。包含当前编译系统，编译工具，编译参数，编译输出目录，所以首先在build_common.py要提供类似编译设定
* 通用CMAKE_BUILD_TYPE=debug，
* 系统相关的通用设置如win32下，默认arch=AMD64，而android下，默认arch=arm64。
* 系统特定设置，如win32下，添加PROJECT(如vs2019/vs2022)，android下，添加CMAKE_TOOLCHAIN_FILE=android.toolchain.cmake,ANDROID_TOOLCHAIN,ANDROID_PLATFORM等设置。
2. 特定的bulid_project.py脚本，添加特定的编译选项。