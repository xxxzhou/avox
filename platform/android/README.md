# AVOX

## 注意事项

* local.properties里的SDK目录要与当前使用SDK匹配
* 切换.gradle版本后，最好把原项目下的.gradle文件删除。

## 问题记录

用vscode里的build_android.py可以编译，但是android studio一直编译不了，查了二天，android studio重装了几次，gradle/ndk/jdk/sdk分别使用不同版本各试了多次，最后发现主要问题有三点。

* 一是其默认生成NDK目录使用\,导致后面相应目录应用出错，需要在相应的build.gradle里指定-DANDROID_NDK=${ndkPath}"。
* 二是Sync build.gradle文件时，提示failed to configure C/C++/lateinit property sysroot has not been initialized，需要在build.gradle里cmake指定DCMAKE_SYSROOT=${ndkPath}/sysroot。
* 三是其会检查C++/C编译器，生成一个类似 CMakeFiles/cmTC_be432.dir/testCCompiler.c.o目录，用编译器去编译这个文件，但是android studio实现没有生成，不知是什么原因，导致这个检查一直不能通过，使用CMAKE_C_COMPILER_FORCED=TRUE/CMAKE_CXX_COMPILER_FORCED=TRUE可以强制关闭检查。
* 新版本android studio里编译发现不带标准库目录，需要手动添加

## 执行命令

记录android studio中 cmake位置。
``` txt
C:\Users\mfjt5\AppData\Local\Android\Sdk\cmake\3.22.1\bin\cmake.exe
```

如下是android studio生成的cmake命令。

cmake.exe -HD:\Work\github\avox -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_SYSTEM_VERSION=21 -DANDROID_PLATFORM=android-21 -DANDROID_ABI=arm64-v8a -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a -DCMAKE_ANDROID_NDK=C:/Users/mfjt5/AppData/Local/Android/Sdk/ndk/21.0.6113669 -DCMAKE_CXX_FLAGS=-std=c++17 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=D:\Work\github\avox\platform\android\AvoxJava\avox\build\intermediates\cxx\Debug\5z473kq1\obj\arm64-v8a -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:\Work\github\avox\platform\android\AvoxJava\avox\build\intermediates\cxx\Debug\5z473kq1\obj\arm64-v8a -DCMAKE_BUILD_TYPE=Debug -BD:\Work\github\avox\platform\android\AvoxJava\avox\.cxx\Debug\5z473kq1\arm64-v8a -GNinja -DANDROID_STL=c++_shared -DANDROID_TOOLCHAIN=clang -DCMAKE_SYSTEM_NAME=Android -DCMAKE_MAKE_PROGRAM=C:/Users/mfjt5/AppData/Local/Android/Sdk/cmake/3.22.1/bin/ninja.exe -DANDROID_NDK=C:/Users/mfjt5/AppData/Local/Android/Sdk/ndk/21.0.6113669 -DCMAKE_TOOLCHAIN_FILE=C:/Users/mfjt5/AppData/Local/Android/Sdk/ndk/21.0.6113669/build/cmake/android.toolchain.cmake

我靠啊，android studio一直编译不了，是因为WebRTC的ninja与Android的ninja混合了。

