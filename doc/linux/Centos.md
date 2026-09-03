# 管理员

[Linux命令之修改/etc/sudoers文件visudo](https://blog.csdn.net/cnds123321/article/details/125162222)
进入超级用户模式，输入su -,后面加-表示切换到根目录下。
提示不是is not in the sudoers file。
执行visudo命令修改 /etc/sudoers 文件，直接使用vi修改可能不成功，因为还非管理员以及只读等问题。
在如下行，输入i进入编辑模式。
root    ALL=(ALL)       ALL
添加
当前用户名    ALL=(ALL)       ALL
然后按ESC退出编辑模式，输入:wq保存并退出。   

## 文件操作

[CentOS 7 实战指南：文件操作命令详解](https://blog.csdn.net/fox9916/article/details/135315028)

## 安装

[CentOS 7镜像列表服务下线](https://zhuanlan.zhihu.com/p/707097821)

[CentOS7.5 通过wget下载文件到指定目录](https://blog.csdn.net/xiaocy66/article/details/83058166)

[CentOS7安装Git以及操作](https://blog.csdn.net/xwj1992930/article/details/96428998)

[CentOS7安装Chrome](https://www.cnblogs.com/nuccch/p/15063165.html)

[CentOS7安装配置JDK环境](https://blog.csdn.net/weixin_41394654/article/details/123442460)

[Linux下载Android SDK](https://blog.csdn.net/qq_38182842/article/details/110170630) 在后面添加sudo ./sdkmanager --sdk_root=/opt/android-sdk/ "ndk;21.0.6113669" 直接安装NDK。

[Linux centos7 安装ndk](https://blog.csdn.net/hi_mydear_yuaner/article/details/110927129)

[记一次 Centos7 cmake 版本升级](https://blog.csdn.net/llwy1428/article/details/95473542)

[vscode SSH 连接Linux](https://blog.csdn.net/qq_29856169/article/details/115489702)

SSH设置root用户,User root中的root一定要小写

[vscode-remote SSH 中保存文件时无权限的问题](https://blog.csdn.net/qq_36072670/article/details/140692649?spm=1001.2014.3001.5501)

[CentOS7安装Java还是无法使用javac](https://www.cnblogs.com/flyfish2012/p/9527792.html)

[vlc编译和调试](https://zhuanlan.zhihu.com/p/673789742)

[centos编译安装ffmpeg](https://www.cnblogs.com/myon/p/6438981.html)

## 常用命令

``` bat
1 输出环境变量
# echo $ANDROID_NDK_HOME
2 VSCOED里通过SSH操作文件无权限 后面二参数分别是SSH登陆用户名与文件目录
[zhouxin@localhost gitlab]$ sudo chown -R zhouxin /home/zhouxin/gitlab
3 android NDK/SDK常用环境变量
sudo vim /etc/profile
// 编辑/etc/profile
export ANDROID_HOME=/opt/android/android-sdk
export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools
export ANDROID_NDK="/opt/android/android-ndk/android-ndk-r16b"
export ANDROID_NDK_HOME="/opt/android/android-ndk/android-ndk-r16b"
export PATH="$ANDROID_NDK:$PATH"
// 刷新/etc/profile
source /etc/profile
在ubuntu上直接使用nano ~/.bashrc添加相应的环境变量，然后source ~/.bashrc应用。
4 ERROR: cannot verify cmake.org's certificate,下载时提示SSL证书验证失败
// sudo yum update 这个操作时间特别长
sudo yum reinstall ca-certificates
```

## 编译问题

``` txt
> Task :app:compileReleaseJavaWithJavac FAILED

FAILURE: Build failed with an exception.

* What went wrong:
Execution failed for task ':app:compileReleaseJavaWithJavac'.
> Could not find tools.jar. Please check that /usr/lib/jvm/java-1.8.0-openjdk-1.8.0.412.b08-1.el7_9.x86_64/jre contains a valid JDK installation.

* Try:
Run with --stacktrace option to get the stack trace. Run with --info or --debug option to get more log output. Run with --scan to get full insights.

* Get more help at https://help.gradle.org

Deprecated Gradle features were used in this build, making it incompatible with Gradle 6.0.
Use '--warning-mode all' to show the individual deprecation warnings.
See https://docs.gradle.org/5.4.1/userguide/command_line_interface.html#sec:command_line_warnings

BUILD FAILED in 18s
32 actionable tasks: 32 executed
[root@localhost viotplayersdk]# ls /usr/lib/jvm/java-1.8.0-openjdk-1.8.0.412.b08-1.el7_9.x86_64
jre
[root@localhost viotplayersdk]# java -version
openjdk version "1.8.0_412"
OpenJDK Runtime Environment (build 1.8.0_412-b08)
OpenJDK 64-Bit Server VM (build 25.412-b08, mixed mode)
[root@localhost viotplayersdk]# javac -version
bash: javac: command not found...
Similar command is: 'java'
```
如果两个命令都返回版本信息，那么您已经安装了JDK。如果只有java命令返回版本信息，那么您可能只安装了JRE。
参考[CentOS7安装Java还是无法使用javac](https://www.cnblogs.com/flyfish2012/p/9527792.html)