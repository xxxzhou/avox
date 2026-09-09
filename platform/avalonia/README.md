# 前端界面

为了统一各平台的界面，方便底层在各平台的功能测试，准备使用AI完成大部分界面的编写。

主要用来完成avox在各平台的界面,相关API调用主要是swig/csharp封装的底层C++的C#接口.

## 规范

AI所有实现只有DDPlayer这个文件夹下.

运行android,底层改动大请重新运行build_android.py生成libavox_csharp.so.

### 注意事项

1. 这是对底层C++的功能封装,使用avalonia(C#)框架完成的界面,其C++转C#通过swig完成,所有C++底层结果对应C#的实现都在swig/csharp/files里,结构与字体都需要优先参考这里面,不要生成结构与类.LogLevel.ImageType这些全在AvoxCommon链接的swig/csharp/files里,包含IoPanel也是,不会缺少类的,如果缺少类,直接去swig/csharp/files里找肯定找的到.

2. 数据打印直接用AvoxWrapper.logMsg(LogLevel,string)

3. IOS/Android 平台没有多界面,那么相应设置/打开文件夹等对话框类似的,是不能用的,可能没反应,可能导致crash.

## 流程

swig添加或删除文件后,AvoxCommon的AvoxCommon.csproj链接的swig/csharp/files/AvoxCommon.csproj需要同步更新,可以删除DDPlayer下的.vs文件夹,让AvoxCommon.csproj重新刷新.

## 设计

主要是针对如下几个C++类的再包装.

### 播放器相关

1. IMediaPlayer: 针对需要解码的媒体文件,直播流等,需要有解码器,底层有队列管理原始包,解码后的帧,需要音视频同步.
2. ISourcePlayer: 针对直出的设备IVideoSource/IAudioSource,包含windows的窗口截获,android/ios的相机,各平台的麦等,不需要解码器,也不需要同步,来数据直接渲染.
3. IRtcPlayer: 针对WebRtc协议,其中编码/解码/数据源使用C++项目已实现的再映射成webrtc接口给webrtc使用,余下网络协议,传输,控制等全使用WebRtc自身的.

上面三个全使用IMediaPlayerOb回调,意思相应的播放器事件是用同一接口.

### 渲染

1. ISurfaceRender: 上面三个播放器视频渲染控制,可输入窗口句柄,控制渲染结果,图像处理参数等.
2. IAudioRender: 上面三个播放器的音频渲染控制,音频大小,3A参数等控制.
3. ISourceInfo: 播放器里音频/视频源信息获取,比如分辨率,码率,帧率等.

### 现有问题

刚开始,IMediaPlayer/ISourcePlayer/IRtcPlayer有很多相同点,导致以设计以AvPlayer为主,比如其AvPlayerModel控制AvPlayer来设计,AvRenderView也以AvPlayerModel来设计.

现在重新设计,IMediaPlayer/ISourcePlayer/IRtcPlayer的共同基类只有回调IMediaPlayerOb这部分,余下各接口差异比较大,全部各自实现,其子控件以具体的实现来.

1. BasePlayer: IMediaPlayerOb回调实现.
2. MediaPlayer: IMediaPlayer的接口再封装.
3. SourcePlayer: ISourcePlayer的接口再封装.
4. RtcPlayer: IRtcPlayer的接口再封装.
5. AvRenderView: 以ISurfaceRender为主体封装.
6. PlayerModel: BasePlayer的Model实现.
6. MediaModel: MediaPlayer的Model实现.
7. SourceModel: SourcePlayer的Model实现.
8. RtcModel: RtcPlayer的Model实现.










