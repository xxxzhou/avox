# MAC开发

外接硬盘一定要用MAC的APFS格式,不要用exFAT格式,现阶段外接exFAT会产生._的索引文件无解,会造成各种各样的问题。

## 环境

xcode新版需要先打开，初始化下项目，然后cmake才能调用xcode的工具链。

MAC VPN使用ClashX。

## 编译打包

注意编译目标不要选择模拟机，没有真机就选择Any iOS Device(arm64)

## 封装模块调试

A是原始C++模块，B是A的封装测试模块。

B添加Podfile文件,添加对A模块的路径。

1. 在B.xcworkspace目录下执行pod install(对应上面的Podfile文件)，然后B工程同级Pods文件有A模块lib/include。

2. 找到B工程同级Pods里A模块的lib文件夹，删除a.lib.

3. 在Pod install后，在B工程下的Pods目录下，分别打到B-demo.debug/release，删除-l a,此时编译会提示找不到a的相关符号。

4. 将A工程(.xcodeproj)拖到B工程下(非PODS目录)，然后在B的build Phases下的Target Dependencies添加a的项目依赖(如果a没拖到正确位置，会找不到),这样编译B时，会优先编译A。

5. 在B工程中的General里的Frameworks, Libraries, Embedded Content里添加a的lib引用。

## 清理重新安装POD

1. 进入 testbed 目录
cd /Volumes/PSSD/work/github/avplay/platform/ios/testbed
2. 删除 Pods 目录和 Podfile.lock 文件
rm -rf Pods
rm Podfile.lock
3. 重新安装依赖
pod install

4. 移除项目中的 Pod 集成
pod deintegrate
5. 清除 CocoaPods 缓存
pod cache clean --all
6. 重新安装 Pod
pod install

7. 验证podspec文件是否正确
pod lib lint avoxsdk.podspec --allow-warnings

## 终端命令

如果有权限问题，尝试在命令前加sudo.

``` txt
// 终端是zsh还是bash
echo $SHELL
// 查看环境变量
echo $PATH
printenv
```

添加路径到环境变量，比如xcode的路径
``` txt
~/.zshrc
export PATH="/Applications/Xcode.app/Contents/Developer/usr/bin:$PATH"
// 使环境变量生效
source ~/.zshrc
```

打开arm64 环境的终端
``` txt
打开“终端”应用。
右键点击 Dock 中的“终端”图标，选择“新建命令执行实例”。
在新终端窗口中输入以下命令，进入 arm64 环境：
arch -arm64 /bin/zsh
# 切换到 x86_64 环境
arch -x86_64 /bin/zsh
```

## POD安装 

需要注意,x64_64与arm64对应的包管理器brew是不一样的,需要单独安装。如果没有正确配置环境变量，可能会导致在不同架构环境下调用到错误版本的 Homebrew。例如，在 arm64 环境下意外调用了 x86_64 版本的 Homebrew 来安装软件，可能会安装不兼容的软件包。两个不同架构的 Homebrew 会分别在不同的目录下存储软件包，会占用更多的磁盘空间。arm64 版本默认安装在 /opt/homebrew/opt，x86_64 版本默认安装在 /usr/local/opt。

在不同架构环境下，确保使用对应的 Homebrew。可以在 ~/.zshrc 或 ~/.bash_profile 中添加以下脚本：
``` txt
if [[ $(arch) == "arm64" ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
else
    eval "$(/usr/local/bin/brew shellenv)"
fi
```

pod切换源，先移除源，再添加淘宝镜像，更新本地库。
``` txt
// pod切换源，先移除源
pod repo remove master
// 再添加淘宝镜像
pod repo add master https://gitee.com/mirrors/CocoaPods-Specs.git
// 更新本地库
pod repo update
```

1. 安装Brew(homebrew)管理器。

``` txt
// 安装脚本
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
// 这个脚本可能会连接不上，使用中科大的镜像,跟着脚本的提示一步一步安装
/bin/zsh -c "$(curl -fsSL https://gitee.com/cunkai/HomebrewCN/raw/master/Homebrew.sh)"
// 安装rvm
curl -L get.rvm.io | bash -s stable
// 载入rvm
source ~/.rvm/scriots/rvm
// 更新rvm
rvm get stable
```

2. 安装Ruby,版本最好3.2.0以上，很多第三方库要求Ruby版本3.2.0以上,cocoapods是基于ruby开发的。

``` txt
rvm install ruby-3.2.0
查看版本
ruby -v 
```

3. 安装cocoapods(做为Ruby gem安装)，

``` txt
// 安装
sudo gem install cocoapods
// 查看版本
pod --version
```

## POD打包

1. cd定位到对应目录，比如VioTSDK.xcworkspace外层目录CLSDK，可以直接拖目录到终端。

2. 执行pod install