# WSL2/Ubuntu

## 文档

[超详细Windows10/Windows11 子系统（WSL2）安装Ubuntu20.04（带桌面环境）](https://blog.csdn.net/weixin_44301630/article/details/122390018)

注意检查下是否是WSL2,用wsl -l -v看下是否为2,不是需要升级2.

## WSL2 网络

在wsl2不同wsl使用同一网络,有自己单独的网络.

首先测试wsl是否可以ping通自身wsl

ping -c 3 172.25.48.1,不可以的话,很可能是防火墙的问题.

使用powershell管理员权限运行.
* 关闭防火墙
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False
* 再测试,如果ping通,说明是防火墙
zhouxin@DESKTOP-1QLM5EA:~/work/gitlab/viotplayersdk$  ping -c 3 172.25.48.1
PING 172.25.48.1 (172.25.48.1) 56(84) bytes of data.
64 bytes from 172.25.48.1: icmp_seq=1 ttl=128 time=0.474 ms
64 bytes from 172.25.48.1: icmp_seq=2 ttl=128 time=0.437 ms
64 bytes from 172.25.48.1: icmp_seq=3 ttl=128 time=0.469 ms
* 再打开
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled True
* 添加ICMP允许规则
New-NetFirewallRule -DisplayName "Allow WSL ICMP" -Direction Inbound -Protocol ICMPv4 -IcmpType 8 -Action Allow -Enabled True
Get-NetFirewallRule -DisplayName "Allow WSL ICMP" | Format-Table DisplayName, Enabled, Direction, Action

## 使用windows代理 clash verge

虚拟机连不上google相关的android开发环境，在加上代理后，还是连不上记的clash verge开TUN模式。记的重启。

// 重启
wsl --shutdown
// 如果连不上，nano ~/.bashrc添加代理(WSL2是windows对应的WSL网络IP 类似172.25.48.1)
export http_proxy=http://172.25.48.1:7897
export https_proxy=http://172.25.48.1:7897

curl -I --connect-timeout 10 http://www.google.com:80
curl -I --connect-timeout 10 http://archive.ubuntu.com/ubuntu

但是apt还是一直连不上，需要专门为apt配置代理.
单次 env http_proxy=http://172.25.48.1:7897 https_proxy=http://172.25.48.1:7897 sudo apt update
一直有效,打开创建如下文件.
sudo nano /etc/apt/apt.conf.d/95proxies
输入如下内容
Acquire::http::Proxy "http://172.25.48.1:7897";
Acquire::https::Proxy "http://172.25.48.1:7897";

## 安装流程

1. 更新软件包
   sudo apt update
2. 安装通用软件
   // unzip
   sudo apt install unzip
   // git
   sudo apt install git
   git --version
   // openjdk
   sudo apt install openjdk-8-jdk
   java -version
   javac -version
3. 安装Android SDK
   // Android SDK
   wget https://dl.google.com/android/repository/commandlinetools-linux-6858069_latest.zip
   // 解码到/opt/android-sdk
   sudo unzip -d /opt/android-sdk commandlinetools-linux-6858069_latest.zip
   // 切换到/opt/android-sdk/cmdline-tools/bin
   cd /opt/android-sdk/cmdline-tools/bin
   // 显示SDK列表
   ./sdkmanager --sdk_root=/opt/android-sdk/ --list
   // SDK
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "platforms;android-28"
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "platform-tools"
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "build-tools;28.0.3"
   // NDK
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "ndk;21.0.6113669"
   // webrtc M138需要build36及以上,硬编需要26及以上
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "platforms;android-36"
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "platform-tools"
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "build-tools;36.0.0"
   sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "ndk;26.1.10909125"

## 编辑环境变量

当前用户使用nano ~/.bashrc，ROOT用户sudo nano /root/.bashrc 。

Ctrl+X保存，Y确认，Alt+M选择Unix格式，然后Enter保存退出。

* 重新加载root的配置
sudo -i
source /root/.bashrc

## 添加环境变量

注意不同的用户环境变量不同。

在ubuntu上直接使用nano ~/.bashrc添加相应的环境变量。光标键盘下到最后，复制如下文本粘贴。

export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64/jre
export PATH=$PATH:$JAVA_HOME/bin
export ANDROID_HOME=/opt/android-sdk
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/bin:$ANDROID_HOME/platform-tools
export ANDROID_NDK=/opt/android-sdk/ndk/21.0.6113669
export ANDROID_NDK_HOME=$ANDROID_NDK

Ctrl + X然后Y保存，然后source ~/.bashrc应用。echo $ANDROID_NDK

查看PATH
echo $PATH | tr ':' '\n'

## 生成SSH公钥

ssh-keygen -t rsa -b 4096 -C "mfjt55@163.com"

cat ~/.ssh/id_rsa.pub

## SSH远程登陆

``` bat
// 重启 WSL 网络服务（在 Windows CMD 或 PowerShell 中运行）
wsl --shutdown

// 安装 OpenSSH 服务器
sudo apt install openssh-server

// 启动 SSH 服务
sudo service ssh start

// 检查 SSH 服务状态
sudo service ssh status

// 编辑 SSH 配置文件（如果需要）
sudo nano /etc/ssh/sshd_config
// 允许ROOT登陆
PermitRootLogin yes
// 如下选项去掉注释
Port 22
ListenAddress 0.0.0.0
PasswordAuthentication yes
PubkeyAuthentication yes

# 检查配置文件是否正常
sudo head -30 /etc/ssh/sshd_config

// 重启 SSH 服务（如果修改了配置）
sudo service ssh restart
// 默认启动
sudo systemctl enable ssh

// 登陆
ssh zhouxin@192.168.1.100
ssh -vvv zhouxin@192.168.1.100
ssh root@192.168.1.100
// PermitRootLogin yes如果已经改了，还是说权限问题，root可能被锁
sudo passwd -S root
// 输出里L表示被锁，如下解锁
sudo passwd -u root
// 如果上面还被锁，可能需要改变root密码，new_password替换你想要的密码
echo "root:new_password" | sudo chpasswd 
```

[vscode SSH 连接Linux](https://blog.csdn.net/qq_29856169/article/details/115489702)

SSH设置root用户,User root中的root一定要小写

## git下载

// 切换到当前目录
cd ~
mkdir github
bash compile.sh

## 常用指令

// 查找文件
find . -name "libmk_api.so"

## 问题

### WSL的密钥变化后，SSH连接失效

如果SSH主机是192.168.1.100，那么可以使用以下命令清除SSH主机的密钥：
ssh-keygen -R 192.168.1.100