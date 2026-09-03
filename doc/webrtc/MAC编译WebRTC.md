# MAC编译WebRTC

## 编译遇到的问题

1. 仿问google需要git也能访问.

https://github.com/clash-verge-rev/clash-verge-rev 打开TUN模式.

最好在MAC上的浏览器直接下载安装,通过别的系统下载后飞书传过来的安装有问题,原因不知道.

2. 下载depot_tools,设置环境变量.

``` sh
cd /Volumes/PSSD/work/webrtc
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
# 如果使用zsh
open ~/.zshrc  
# 添加depot_tools到PATH
export PATH="$PATH:/Volumes/PSSD/work/webrtc/depot_tools"
# 重载zsh
source ~/.zshrc
# 查看PATH
echo $PATH | tr ':' '\n'
```

3. 编译说python脚本缺少模块,MAC可能限制python直接安装,使用brew安装python可能识别不了.

使用python虚拟环境安装需要的模块.

``` sh
cd /Volumes/PSSD/work/webrtc
python3 -m venv venv
source venv/bin/activate
pip install setuptools
```

4. 编译说ninja没有安装,需要安装ninja.

直接使用brew安装ninja就可以.
brew install ninja

## 拉代码

参照[WSL编译WebRTC](./WSL编译WebRTC.md),流程.

1. fetch --nohooks webrtc_ios.后面带ios主要是会在当前目录.gclient下加上target_os = ["ios", "mac"].
``` 
solutions = [
  {
    "name": "src",
    "url": "https://webrtc.googlesource.com/src.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
  },
]
target_os = ["ios", "mac"]
```
2. 中断后 gclient sync --nohooks --no-history
3. 切换到所需要分支 git checkout -b m138 branch-heads/7204
4. 拉取第三方库依赖 gclient sync -D(如果第一步没用webrtc_ios,直接在.gclient文件加上target_os = ["ios", "mac"])

## 编译代码

1. float16类型不支持

编译脚本添加mac_sdk_min="10.10".