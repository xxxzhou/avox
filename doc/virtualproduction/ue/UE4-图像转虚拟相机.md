> 整理自 aocec 仓库 `doc/ue4/虚拟相机.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# UE4图像转虚拟相机

## 流程方案

整体流程如下:创建进程共享视频帧队列.一个进程用于向队列写入视频帧,一个进程用于给系统调用虚拟相机的COM组件,COM组件里检查队列里视频帧数据并读取出来给虚拟相机.

需要考虑的几个点.

1. 相机的视频帧格式,如RGB8,YUV422,NV12,为了尽量通用,读取的格式定义YUV422交叉格式,几乎所有相机内部都使用这种格式,因此不会出现读取虚拟相机不能正确解析图片的问题.转为需要考虑有个层可以处理各个输入图像到YUV422交叉格式的转化.

2. 相机的分辨率问题,定义的队列分辨率与输入图像分辨率不同的问题.考虑大分辨率,上面的图像格式转化与分辨率缩放,如使用CPU,会占用大量CPU资源.

3. COM组件里代码不好调试的问题,因此COM组件尽量代码简单些,可以选择封装基础功能完善的库.

如上所述,视频帧数据到来后,使用Aoce已完成的GPGPU滤境层,根据条件确定是否开启缩放,视频格式转化等滤境.而在COM组件中只需要读取帧数据,不在需要针对视频帧数据做任何处理.

虚拟相机COM组件使用libdshowcapture用来封装底层dshow逻辑,其基类完善,外部使用方便,只需要填充帧数据以及视频帧大小变化,其开源协议为LGPL2.1,可以商用.

其COM组件需要注意,32位程序调用对应32位的COM组件,64位程序调用64位COM组件,如果想通用,需要生成32/64的二个版本的COM组件.且还需要注意另外一个问题,进程共享视频帧队列数据结构很可能因32/64不同平台,对应的数据结构长度不一致,如用64位程序写入到进程共享视频帧队列,在32位程序调用COM组件,读取的进程共享视频帧队列因为数据结构长度不一致,指针数据偏移,读出的数据错误,导致读取结果完全错误而Crash,因此需要设计成32/64同样偏移结构数据.

其UE4插件主要完成如下二个功能.

1. 捕获CineCamera图像到RTT,使用一个USceneCaptureComponent2D组件,保证与关联的CineCamera姿态同步,得到USceneCaptureComponent2D对应的RTT.

2. UE4图像GPU数据直接给底层框架,对接之前底层框架UE4的DX11/DX12的GPU交互实现.

[aoce_win/vcam](../../code/aoce_win/vcam)封装进程共享视频帧队列,以及输入数据到进程共享视频帧队列,包含GPU图像缩放/YUV各格式转化,UE4纹理输入功能整合.

[aoce_win_dshow/virtualcam](../../code/aoce_win_dshow/virtualcam)作为COM组件,读取共享视频帧队列里面视频帧.

## UE4激活不确定动画

有一个需求,多个动作可以按键激活,其动作间如何自然变化.使用状态机与蒙太奇都可以让动作连续,经测试播放蒙太奇会覆盖Live Link Face动画状态,只有状态机上可以混合,但是现在问题是动作是不确定的,还在调整中,如果用状态机,就算9个动画,都可以整出快百种状态,再加后面一改,可以想像要浪费多少时间,并且状态一多容易出错,查找又要花费时间.

所以需要用程序变量的思路去解决,注意在状态机里有很多功能限制,如不能给变量赋值,不能调用大部分蓝图执行函数,所以要把状态机里功能与变化分离.

根据需求,先确定二种状态,一是Idle,二是用户快捷激活对应动作,如果只是二种状态,那从Idle->激活State/激活State-Idle二种能正常变化,但是如果在非Idle状态下,激活State变成另一种激活State的状态,这是动作就会没有中间变化,导致动作显示出问题,解决方法是加入第三种状态,用于解决激活State变成另一种激活State的状态的情况,先看状态机如下.


根据需求定义接口用于外部通知当前状态变化,主要包含SetAnimIdle设置初始动画,UpdateCurrent按键激活动画,BackIdle回到初始动画状态,其动画蓝图里对应接口实现如下.


输入动画激活调用相关接口实现.


## 参考文档

[MFVirtualCamera](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera)

[windows驱动开发8：虚拟摄像头方案](https://blog.csdn.net/longkuis/article/details/127655948)

[winoows10驱动开发](https://github.com/Microsoft/Windows-driver-samples)

[如何实现DirectShow source filter](https://www.jianshu.com/p/42489956f866)

This library is built from directshow base classes, you can find the source code from windows sdk 7.1 samples (\multimedia\directshow\baseclasses).
[Check msdn for more detail](https://docs.microsoft.com/en-us/windows/desktop/directshow/using-the-directshow-base-classes).

[obs-virtual-cam](https://github.com/stream-labs/obs-virtual-cam)

OBS最新使用如下第三方库[libdshowcapture](https://github.com/obsproject/libdshowcapture/tree/2fa2e488b7dc4a266eb8f5be56ced626e39a6159)

[创建COM组件全过程](https://blog.csdn.net/henry000/article/details/7008397)
