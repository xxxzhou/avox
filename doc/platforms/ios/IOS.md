# IOS

## 文档

[Objective-C 入门教程](https://www.runoob.com/w3cnote/objective-c-tutorial.html)

[iOS 开发应该如何入门？](https://www.zhihu.com/question/474552596)

[vulkan安装](https://vulkan.lunarg.com/sdk/home#mac)

[vulkan ios版本](https://github.com/KhronosGroup/MoltenVK/releases)

[iOS开发 - 超详细集成 FFmpeg 步骤](https://www.jianshu.com/p/ecfbebadbe55)

[Direct Access to Video Encoding and Decoding](https://devstreaming-cdn.apple.com/videos/wwdc/2014/513xxhfudagscto/513/513_direct_access_to_media_encoding_and_decoding.pdf?dl=1)

## ffmpeg

[ffmpeg-ios](https://github.com/guanweidong/ffmpeg7.0)

2. 查找 .tbd 文件
找到 iOS SDK 路径后，在该路径下的 usr/lib 目录里查找 libz.tbd、libbz2.tbd 和 libiconv.tbd 文件。你可以使用 find 命令进行查找：

``` sh
bash
SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path)
find "$SDK_PATH/usr/lib" -name "libz.tbd" -o -name "libbz2.tbd" -o -name "libiconv.tbd"
```

命令解释 SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path)：将当前使用的 iOS SDK 路径赋值给 SDK_PATH 变量。
find "$SDK_PATH/usr/lib"：在 iOS SDK 的 usr/lib 目录下进行查找。
-name "libz.tbd" -o -name "libbz2.tbd" -o -name "libiconv.tbd"：查找名称为 libz.tbd、libbz2.tbd 或 libiconv.tbd 的文件，-o 表示逻辑或。

## Vulkan安装

[vulkan](https://github.com/KhronosGroup/MoltenVK/releases)在MAC上安装vulkan,如果有IOS开发,记的勾选组件IOS,安装完成后,需要确执行了install_vulkan.py,如果有权限问题,用sudo安装,如果安装失败,再次安装前需要先执行uninstall.sh,执行成功会把相关的lib放入/usr/local/lib中,最后执行setup-env.sh把VULKAN_SDK添加到环境变量.

## Zlmediakit

编译zlmediakit里提示有armv7过时架构不能编译,找到对应CMakeCache.txt里的CMAKE_OSX_ARCHITECTURES:STRING=armv7;armv7s;arm64,移除armv7;armv7s.后面发现更新ios.toolchain.cmake更方便,其老的ios.toolchain里有armv7,新的已经没有了.

## 开发问题

1. 提示找不到C/C++编译器.

直接在cmake里指定CMAKE_CXX_COMPILER/CMAKE_C_COMPILER.

2. error: An empty code signing identity is not valid when signing a binary for the 'Dynamic Library' product type

[Xcode 10.2](https://iosre.com/t/xcode-102/14369/3)

Xcode10.2后,最新Xcode,证书不能直接 None, 必须要有设置开发证书才能使用,cmake里对应set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO").

3. 在非IOS硬盘格式改动文件,会生成._的appledouble文件,导致编译失败.

find . -name "._*" -delete

4. 查看dylib的架构

cd /opt/homebrew/opt

lipo -info molten-vk/lib/libMoltenVK.dylib

5. 查看dylib依赖

otool -L /Volumes/PSSD/work/github/avox/build/ios/avox/install/aarch64/Debug/libavox.dylib

6. 查看.a文件的导出的函数

nm -gU /Volumes/PSSD/work/github/avox/build/ios/avox/install/aarch64/Debug/libavox.a
// 若要查看原始的函数名
nm -gU /Volumes/PSSD/work/github/avox/build/ios/avox/install/aarch64/Debug/libavox.a | c++filt
// 查询是否包含avox::timeTickMS方法
nm -gU /Volumes/PSSD/work/github/avox/build/ios/avox/install/aarch64/Debug/libavox.a | c++filt | grep "avox::timeTickMS"

6. 引用的MoltenVK库,found architecture 'x86_64', required architecture 'arm64'

简单来说,默认打开的zsh终端是x86_64,需要在终端里切换到arm64环境,使用brew重新安装MoltenVK,其arm64 版本默认安装在 /opt/homebrew/opt，x86_64 版本默认安装在 /usr/local/opt.

7. avox本身能正常编译,但是别的项目引入avox,提示cmath找不到

.h文件不引入<cmath>,而是引入<math.h>.

8. .m文件引用的.h文件里有命令空间,编译不通过

把.m文件改为.mm,在.mm文件里引入.h文件.

10. -libtool: can't locate file for: -lPods-xxx

把相应的sdk下的Podfile对应xcode项目Frameworks里产生的-lPods-xxx.a文件删除掉就行.

11. xcode调试全是汇编

菜单debug->Debug Workflow->Always Show Disassembly 勾给去掉。

12. POD带非系统Frameworks如MoltenVK,报错 Sandbox: rsync.samba(95988) deny(1) file-write-create

在运行的项目里Build Settings,找到Build Otions把User Script Sanboxing 改为NO.

13. Xcode项目手动配置引用avox运行时依赖配置说明
在 Xcode 项目中，需要进行如下配置以确保 `MoltenVK.framework` 能正常加载：
1. 在 `General` 选项卡的 `Frameworks, Libraries, and Embedded Content` 部分，将 `MoltenVK.framework` 的嵌入选项设置为 `Embed & Sign`。
2. 在 `Build Settings` 选项卡中，搜索 `Runpath Search Paths`，添加 `@executable_path/Frameworks`。

14. 使用zlmediakit拉流,crash,报 Linked against modern SDK, VOIP socket will not wake. Use Local Push Connectivity instead.

[session.isVOIPEnable = false](https://github.com/MailCore/mailcore2/issues/1963)

[解决CocoaAsyncSocket在iOS16系统上的崩溃问题](https://juejin.cn/post/7143124820811579423)

这边是尝试直接把ZLToolKit里的Socket_ios.mm里的kCFStreamNetworkServiceTypeVoIP改为kCFStreamNetworkServiceTypeBackground后正常,试过改PKPushTypeVoIP但是编译不过.


