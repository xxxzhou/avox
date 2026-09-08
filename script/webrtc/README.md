# WebRTC编译

## 脚本

不同的使用对应webrtc_build_platform,如windows平台使用build_webrtc_win.ps1.

脚本先需要修改WEBRTC_DIR目录,对应你下载的WebRTC目录,下面有src表示webrtc源码目录。

windows下使用 .\webrtc_build_windows.ps1 release.就会编译release版本的webrtc库。bat因为生成和编译会断开,需要手动ninja -C.

编译成功后脚本会额外产出无符号轻量版(`libwebrtc_nosym.a`/`webrtc_nosym.lib`, strip掉DWARF/CodeView调试信息,体积约为带符号版的1/4~1/14,发布分发用)。带符号版体积大但崩溃可直接符号化,`FindWebRTC.cmake` 默认链接带符号版。

## Android

编译webrtc的android库，需要使用linux环境。使用windows编译，会出现断言错误assert(host_os == "linux", "Android builds are only supported on Linux.")。

也不要想着在WSL上访问windows上的webrtc源码编译,坑多的想不到,老老实实在WSL上拉M138的源码.

使用[WSL2](../../doc/platforms/linux/Ubuntu.md),安装wsl2/ubuntu及android ndk相关环境。[WSL编译WebRTC](../../doc//webrtc/WSL编译WebRTC.md)

windows与wsl2使用不同的换行，导致编译时出现错误，所以最好使用WSL2重新拉取代码，然后在WSL2编译。

## IOS

好像只能在MAC上编译,普通过程参看上面Android,其余查看[MAC编译WebRTC](../../doc/webrtc/MAC编译WebRTC.md)

## Mac

macOS本机库(arm64),在Mac上使用 `webrtc_build_mac.sh release` 编译。输出目录为 `build/darwin/<type>`,目录名darwin与FindWebRTC.cmake的CMAKE_SYSTEM_NAME小写一致,编完拷贝 `obj/libwebrtc.a` 到 `<avc_library>/build/darwin/release/`。

注意: depot_tools里的ninja只是壳,需要 `brew install ninja` 的真ninja(脚本已把/opt/homebrew/bin加进PATH)。

## 文档

[webrtc分支](https://chromiumdash.appspot.com/branches)

## git

// windows平台使用webrtc默认预设，android平台使用webrtc_android预设
fetch --nohooks webrtc
// 抓7204分支，创建名为138的分支
git checkout -b m138 branch-heads/7204
// 并同步（此时同步默认执行hooks）
gclient sync -D

## 注意

* 查看webrtc依赖的库，使用x64 Native Tools Command Prompt for VS 20XX,以管理员启动  