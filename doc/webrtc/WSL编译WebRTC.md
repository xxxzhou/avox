# WSL编译WebRTC

[WebRTC M137 Android版本编译](https://blog.csdn.net/rosyrays1/article/details/148355150)
[WebRTC安卓编译](https://blog.jianchihu.net/webrtc-android-build-guide.html)
[编译源码](https://webrtc.mthli.com/basic/webrtc-compilation/)

可能需要wsl2,wsl在网络使用更方便与稳定,但是会有些webrtc需要的库安装更新有问题,无奈最后还是升wsl2,还有不要直接全用root账号来操作,开始前看是方便了,到后面直接提示你不能用ROOT来编译,然后文件权限还出问题了,所以一开始就需要默认用登录用户操作.

## 环境准备

* 更新包管理器
sudo apt update && sudo apt upgrade -y

* 安装必要的依赖
sudo apt install -y git python3 python3-pip curl wget unzip

## 安装depot_tools

所有webrtc相关命令,如下载执行同步等相关操作没必要不要用sudo,不然特别多的权限问题.

// webrtc清华镜像
// git remote add tuna https://mirrors.tuna.tsinghua.edu.cn/git/webrtc.git

* 同步webrtc代码不能使用ROOT用户，需要使用普通用户，因此在开始前需要先切换成普通用户。
su zhouxin

* 克隆depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git，不要使用windows下的depot_tools，会出现一些奇怪的问题，比如报cpid重命令无权限，但是实际和权限没关系。

* 添加到PATH环境变量(路径有特殊字符，需要""括起来)
echo 'export PATH=$PATH:/home/zhouxin/work/webrtc/depot_tools' >> ~/.bashrc
// 放入最前面，因为WSL会套用windows的PATH，放后面会导致wsl使用windows的depot_tools
echo 'export PATH=/home/zhouxin/work/webrtc/depot_tools:$PATH' >> ~/.bashrc
source ~/.bashrc
* 查看PATH 
echo $PATH | tr ':' '\n'

有时加不上去depot_tools，可以在当前进程中export PATH="$PATH:/home/work/zhouxin/webrtc/depot_tools"，先在当前进程中使用。如果有权限或是网络问题，可以尝试加上 export DEPOT_TOOLS_UPDATE=0 。

## 拉取webrtc代码

* 获取WebRTC针对webrtc_android预设配置
fetch --nohooks webrtc_android

* 如果中断了，重新同步代码（这需要较长时间）
gclient sync
* 重复拉取多次，不会重复下载 
gclient sync --nohooks --no-history

// 抓7204分支，创建名为138的分支 切到src目录下
// 注意,WSL通过windows再代理访问网络不太稳定
// 切到138的分支后,一定要git status查看状态是否切成功
git checkout -b m138 branch-heads/7204
// 并同步（此时同步默认执行hooks）切到webrtc目录
gclient sync
 
## 依赖安装

cd src
// 安装编译 WebRTC 所需的依赖
./build/install-build-deps.sh --arm
./build/android/envsetup.sh
echo $PATH | tr ':' '\n'

如果还缺少文件或是库,与src同级的.gclient文件,加上target_os = ["android"],再gclient sync.

## 编译

gn选项可以在$WEBRTC_DIR/src下使用gn args --list ../build/$platform/$build_type查看.

安卓libwebrtc的依赖项实际上包含了一个定制版的LLVM编译器工具链.当你用它构建代码时,所有内容都会生成在基于Chromium的专用“CR_”命名空间中,因此你的STL对象均不匹配.你的代码 STL依赖于Android NDK提供的标准常规命名空间符号,但libwebrtc.a却需要一个完全不同的命名空间.

因此，你有两种解决方案：
1. 使用定制版 libwebrtc 工具链构建代码
2. 采用标准 Android NDK 工具链构建其代码

目前来说,二种方案问题都很多,第二种方案越早的版本越容易,第一种方案越新的版本越容易.

android webrtc动态库没有导出符号,不然也可尝试一下.

[android webrtc使用标准clang编译](https://issues.webrtc.org/issues/42223745#comment9)

[2025 年为 Android 编译 WebRTC 静态二进制文件](https://www.nxrte.com/jishu/webrtc/61097.html)

查看SO库导出
readelf -Ws build/android/release/libjingle_peerconnection_so.so | grep -E "(FUNC|OBJECT)"

第一种方案在于各种神仙错误,把整个编译链换到WebRTC下,没这方面专业知识,不清楚错误指那里,只能放弃.

暂时只有第二种方案勉强能在M138通过,主要问题如下.

1. use_custom_libunwind为true时,提示找不到_Unwind_Backtrace/_Unwind_GetIP,问题应该是没用webrtc内置的编译链,但是用了内置的unwind,按照[2025 年为 Android 编译 WebRTC 静态二进制文件](https://www.nxrte.com/jishu/webrtc/61097.html)试过不得行,可能一个是137,这边现在是138.

2. use_custom_libunwind也用false,就会提示ld.lld: error: unable to find library -l:libunwind.a,测试在src/build/config/compiler/BUILD.gn下,添加如下代码可以跑通.

``` txt
zhouxin@DESKTOP-1QLM5EA:~/work/github/avox$ find /opt/android-sdk/ndk -name "libunwind*"
/opt/android-sdk/ndk/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/i386/libunwind.a
/opt/android-sdk/ndk/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/riscv64/libunwind.a
/opt/android-sdk/ndk/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/x86_64/libunwind.a
/opt/android-sdk/ndk/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/arm/libunwind.a
/opt/android-sdk/ndk/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64/libunwind.a
/opt/android-sdk/ndk/21.0.6113669/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libunwind.a
/opt/android-sdk/ndk/21.0.6113669/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/arm-linux-androideabi/libunwind.a

经测试,放26的aarch64可以,放21的都不行.
```

``` gn
config("runtime_library") {
    ...
if (is_posix || is_fuchsia) {
    configs += [ "//build/config/posix:runtime_library" ]

    if (use_custom_libunwind) {
      # Instead of using an unwind lib from the toolchain,
      # buildtools/third_party/libunwind will be built and used directly.
      ldflags = [ "--unwindlib=none" ]
    } else {
      # --- 新增部分开始 ---
      # 当不使用自定义 unwind 库且是 Android arm64 时，强制指定 NDK 搜索路径
      if (is_android && target_cpu == "arm64") {
        ldflags = [    
          "-L/opt/android-sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64",
        ]
      }
    }
  }    
}
```

3. 尝试直接设置android_ndk_root=\"/opt/android-sdk/ndk/21.0.6113669\" 又编译到一部分就出错,换21.0.6113669 26.1.10909125都不行.

## 问题

1. 下载时报 rename directory 权限出错。

[P540 17:22:43.784 deployer.go:388 I] [unzip] Moving files to their final destination...
[P540 17:22:43.786 deployer.go:403 I] [unzip] Cleaning up...
[P540 17:22:43.786 deployer.go:409 I] [unzip] Deployed infra/3pp/tools/virtualenv:JSnVx-Qo0tlD_pwiRH9M-K_vi-w55zXeMJcEF9TkjiQC in 0.0s
[P540 17:22:43.787 client.go:2189 I] All changes applied.
[P540 17:22:43.789 main.go:1775 I] Removing cipd metadata
[P540 17:22:43.802 fs.go:455 W] fs: failed to rename directory "/home/zhouxin/.cache/vpython-root.1000/store/virtualenv+n21fvm7m09pn5o63l11eh3li0k/contents/.cipd": rename /home/zhouxin/.cache/vpython-root.1000/store/virtualenv+n21fvm7m09pn5o63l11eh3li0k/contents/.cipd /home/zhouxin/.cache/vpython-root.1000/store/virtualenv+n21fvm7m09pn5o63l11eh3li0k/contents/4pf1OXjUIdf2: permission denied

问题原因（简短）
vpython 在解压/重命名缓存目录时权限被拒绝 —— 很可能是之前以 root/其他用户运行过 fetch/gclient，使得缓存目录被 root 拥有，当前普通用户无权重命名/删除，导致 gclient sync 失败。解决办法：以普通用户运行并修复缓存目录权限或清空缓存。

建议的修复步骤（在 WSL 中以你的普通用户运行）：
```bash
# 切换到普通用户（不要用 root）
# 确认当前用户
whoami
# 查看有问题的目录和权限
ls -la ~/.cache | grep vpython-root
# 把缓存目录的所有权改回当前用户（安全做法）
sudo chown -R "$(id -un)":"$(id -gn)" ~/.cache/vpython-root.*
# 或直接删除有问题的缓存（注意：会重新下载）
rm -rf ~/.cache/vpython-root.*
# 然后以普通用户重新运行 gclient sync
gclient sync
```

如果还没用，跳过vpython.

```bash
# 停止可能的进程，防止占用文件夹
pkill -f vpython
pkill -f gclient
# 跳过vpython
export VPYTHON_SKIP_CHECKS=1
export DEPOT_TOOLS_USE_VPYTHON=0
export VPYTHON_BYPASS="manually managed python not supported by chrome operations"
export DEPOT_TOOLS_UPDATE=0
export GCLIENT_PY3=1
# 重新初始化gclient
fetch --nohooks webrtc
```

