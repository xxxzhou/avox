# Avalonia跨平台播放器开发

在差不多完成各个平台播放器的功能后,想重新写个相对完整的demo,用来演示与测试完成的各个功能,之前各平台如windows用win32/winform,ios用objc,android用java实现了各自原生界面播放流媒体的测试,之后在音频源,视频源,推拉流,双向通话等各个功能越来越多的基础上,每个平台都需要扩展测试,并且之后还要加入linux/mac,太麻烦了,不想用C++写界面相关的,首先AI生成的C++没别的语言好用,二是麻烦,这样就不考虑QT了,刚好前不久看到avalonia,仔细查看[avalonia的文档](https://docs.avaloniaui.net/zh-Hans/docs/overview/supported-platforms)及用avalonia实现的开源项目后,认为可行,如下是当前用avalonia在各平台调用底层C++实现的播放器前端,前期用来验证方案可行性.

<div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;">
  <img src="../../assets/images/avalonia/20260106-110423.png" alt="Windows" style="max-width:50%;height:500;">
  <img src="../../assets/images/avalonia/20260106-105137.png" alt="IOS" style="max-width:24%;height:500;">
  <img src="../../assets/images/avalonia/20260106-110340.png" alt="Android" style="max-width:24%;height:500;">
</div>

记录下整个实现过程,首先前期准备与分析,avalonia是用C#实现的,用的netcore跨平台,大致需要考虑的问题如下:

1. 实现的C++底层库如何让各平台用C#调用.
2. android平台底层C++与java的互交互,比如音频播放与录音是用C++调用java库来实现,而初始化需要把java的activity传入到底层C++初始化等,这些在avalonia中用C#如何实现?
3. ios平台,项目链接很多静态库及框架,如何在avalonia中调用?
4. C++底层用vulkan/平台原生渲染SDK建立Swapchain,在windows用窗口句柄,android用ANativeWindow,ios用CAMetalLayer,如何在avalonia中实现并传入到底层?

好在现在AI足够智能,能快速根据输出信息给出有建设性的建议及查找方向,不然上面的问题还真不好解决.

## swig/csharp

首先是swig,这个是C++转C#的工具,主要是为了方便C#调用C++的库,单独拿出来说,主要是因为原来只是windows平台调用,现在各个平台都是用C#实现,就需要把C++转C#的代码适配在各个平台.

swig转C#主要利用各语言与C语言的交互,各语言与C语言都能直接交互.由于C++在不同编译器和平台间的二进制接口(ABI)不兼容,而C ABI在所有平台上都是稳定的,所以swig把C++转C#时,会先将C++接口转换为C接口,具体来说,把C++中的类和结构体转换为void指针,方法转换为带特定名称的函数,通过C ABI确保跨平台二进制兼容性.底层仍是C++库,C#端使用P/Invoke调用这些C接口,并生成对应的C#类型封装,最后在C#层面将void还原为具体的类型,提供类型安全的调用方式,如下流程.

``` C++
// C++ 播放器
class IMediaPlayer {
public:
  IMediaPlayer() = default;
  virtual ~IMediaPlayer() = default;

public:
  // 返回内置的参数设置器
  virtual IOption *getOption() = 0;
  // 播放器埋点信息返回
  virtual void setPingbackOb(IPingbackOb *ob) = 0;
  // 这个选项确定使用的IO方案(ffmpeg/zlmediakit/webrtc),下次打开启用
  virtual void setIoPlan(IoPlan plan) = 0;
};

// P/Invoke C++转C方式调用
SWIGEXPORT void * SWIGSTDCALL CSharp_AvoxNet_IMediaPlayer_getOption(void * jarg1) {
void * jresult ;
avox::IMediaPlayer *arg1 = (avox::IMediaPlayer *) 0 ;
avox::IOption *result = 0 ;

arg1 = (avox::IMediaPlayer *)jarg1; 
result = (avox::IOption *)(arg1)->getOption();
jresult = (void *)result; 
return jresult;
}
SWIGEXPORT void SWIGSTDCALL CSharp_AvoxNet_IMediaPlayer_setPingbackOb(void * jarg1, void * arg2) {
avox::IMediaPlayer *arg1 = (avox::IMediaPlayer *) 0 ;
avox::IPingbackOb *arg2 = (avox::IPingbackOb *) 0 ;

arg1 = (avox::IMediaPlayer *)jarg1; 
arg2 = (avox::IPingbackOb *)jarg2; 
(arg1)->setPingbackOb(arg2);
}
SWIGEXPORT void SWIGSTDCALL CSharp_AvoxNet_IMediaPlayer_setIoPlan(void * jarg1, int jarg2) {
avox::IMediaPlayer *arg1 = (avox::IMediaPlayer *) 0 ;
avox::IoPlan arg2 ;

arg1 = (avox::IMediaPlayer *)jarg1; 
arg2 = (avox::IoPlan)jarg2; 
(arg1)->setIoPlan(arg2);
}

// P/Invoke C#
[global::System.Runtime.InteropServices.DllImport("avox_csharp", EntryPoint="CSharp_AvoxNet_IMediaPlayer_getOption")]
public static extern global::System.IntPtr IMediaPlayer_getOption(global::System.Runtime.nteropServices.HandleRef jarg1);
[global::System.Runtime.InteropServices.DllImport("avox_csharp", EntryPoint="CSharp_AvoxNet_IMediaPlayer_setPingbackOb")]
public static extern void IMediaPlayer_setPingbackOb(global::System.Runtime.InteropServices.andleRef jarg1, global::System.Runtime.InteropServices.HandleRef jarg2);
[global::System.Runtime.InteropServices.DllImport("avox_csharp", EntryPoint="CSharp_AvoxNet_IMediaPlayer_setIoPlan")]
public static extern void IMediaPlayer_setIoPlan(global::System.Runtime.InteropServices.andleRef jarg1, int jarg2);

// 把C相关函数合成C#对象
public class IMediaPlayer : global::System.IDisposable {
  private global::System.Runtime.InteropServices.HandleRef swigCPtr;
  protected bool swigCMemOwn;
  public virtual IOption getOption() {
    global::System.IntPtr cPtr = AvoxWrapperPINVOKE.IMediaPlayer_getOption(swigCPtr);
    IOption ret = (cPtr == global::System.IntPtr.Zero) ? null : new IOption(cPtr, false);
    return ret;
  }

  public virtual void setPingbackOb(IPingbackOb ob) {
    AvoxWrapperPINVOKE.IMediaPlayer_setPingbackOb(swigCPtr, IPingbackOb.getCPtr(ob));
  }

  public virtual void setIoPlan(IoPlan plan) {
    AvoxWrapperPINVOKE.IMediaPlayer_setIoPlan(swigCPtr, (int)plan);
  }
}
```

整个流程差不多就是这样,回调类的实现会有些特殊,这个有机会再展开说,C++转java/js也是类似的流程,那在这里,就需要知道,swig中C++转C#中,首先是C化,也是会编译成C++库的,如在windows平台,会编译成dll的动态库,android是so的动态库,ios是.a的静态库,IOS用静态库是因为签名等问题,需要把静态库编译到APP里,那么就需要针对ios平台做特殊改动.

``` cmake
# windows/android/ios都生成C++/C#绑定给avalonia使用
if(AVOX_ENABLE_SWIG_CSHARP)   
    message(STATUS "SWIG C# enabled, generating C# bindings...")
    set(CMAKE_SWIG_OUTDIR "${CMAKE_CURRENT_SOURCE_DIR}/csharp/files")
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -namespace AvoxNet)
    if(APPLE)
        # 静态库,需要从avox_csharp改成__Internal
        list(APPEND CMAKE_SWIG_FLAGS "-dllimport" "__Internal")
    endif()   
    set_source_files_properties(${WRAPPERLIST} PROPERTIES CPLUSPLUS ON) 
    # list(APPEND CMAKE_SWIG_FLAGS "-includeall")
    if(APPLE)
        swig_add_library(avox_csharp LANGUAGE csharp TYPE STATIC SOURCES ${WRAPPERLIST})
    else()
        swig_add_library(avox_csharp LANGUAGE csharp SOURCES ${WRAPPERLIST})
    endif()   
    target_link_libraries(avox_csharp PRIVATE avox)    
    if(APPLE) 
        # 针对 iOS 强制指定静态库属性，防止其被识别为 dylib/bundle
        set_target_properties(avox_csharp PROPERTIES 
            POSITION_INDEPENDENT_CODE ON PREFIX "lib" SUFFIX ".a"            
            MACH_O_TYPE staticlib)
    endif()
    if(ANDROID)
        # 前缀为lib
        set_target_properties(avox_csharp PROPERTIES PREFIX "lib")
    endif()
    avox_output(avox_csharp)     
    # avalonia的AvoxCommon直接引用产生的cs文件链接
    # 这个工程只给winfrom使用
    if(EXISTS "${CMAKE_SWIG_OUTDIR}" AND WIN32)        
        add_subdirectory(csharp) 
    endif()
endif()
```

因为在IOS平台,是编译成静态库的,所以相应的P/Invoke不去调用avox_csharp,静态库是直接链接到最后生成的app上,所以P/Invoke的函数名要改成__Internal.

APPLE平台生成封装的C#代码如下:

``` C#
  [global::System.Runtime.InteropServices.DllImport("__Internal", EntryPoint="CSharp_AvoxNet_IMediaPlayer_getOption")]
  public static extern global::System.IntPtr IMediaPlayer_getOption(global::System.Runtime.InteropServices.HandleRef jarg1);

  [global::System.Runtime.InteropServices.DllImport("__Internal", EntryPoint="CSharp_AvoxNet_IMediaPlayer_setPingbackOb")]
  public static extern void IMediaPlayer_setPingbackOb(global::System.Runtime.InteropServices.HandleRef jarg1, global::System.Runtime.InteropServices.HandleRef jarg2);

  [global::System.Runtime.InteropServices.DllImport("__Internal", EntryPoint="CSharp_AvoxNet_IMediaPlayer_setIoPlan")]
  public static extern void IMediaPlayer_setIoPlan(global::System.Runtime.InteropServices.HandleRef jarg1, int jarg2);
```

android因为和windows一样,用的动态库调用的,所以和windows差不多,就不展开说了.

## 播放器框架

这里顺便尝试AI能做什么程度,在这用Trae管理播放器项目,在.trae/rules下新建project_rules.md文件,规定命名,修改范围,注释,README框架设计,由Trae生成大部分代码,在生成的代码上进行细节调整.

告诉Trae,有三个项目,其AvoxCommon是在生成的C#文件swig/csharp/files基础上再封装,AvoxControls是各个子控件的实现,包含播放器模型-视图-视图模型实现,AvaPlayer则是Avalonia Cross Platform Application模板方案,包含AvaPlayer,AvaPlayer.Desktop,AvaPlayer.Android,AvaPlayer.iOS四个模块.

没什么好说的,全是AI生成的代码,实现的还不错,不满意的地方加到project_rules.md规则里,说下个人介入的地方.

首先是AvoxCommon,以链接的方式引入swig生成的c#代码,修改AvoxCommon.csproj文件加入如下内容.

``` xml 
  <ItemGroup>
    <!-- 使用通配符包含目录下所有 cs 文件 -->
    <!-- Link 属性的作用是在 IDE（如 VS/Rider）中显示一个虚拟的文件夹结构，而不改变文件物理位置 -->
    <Compile Include="..\..\..\..\swig\csharp\files\*.cs">
      <Link>AvoxNet\%(RecursiveDir)%(FileName)%(Extension)</Link>
    </Compile>
  </ItemGroup>
```

AvoxControls是播放器的具体控制实现,其中渲染窗口需要用到各平台原生窗口,先不具体展开,只说针对AvoxControls.csproj针对不同平台需要的修改.因为里面有针对不同平台的条件编译,所以 TargetFramework需要改成TargetFrameworks,在这里有net8.0-android;net8.0-ios,然后才能在项目里用#if ANDROID/IOS对应的编译符.因为MAC上没装android相应SDK,所以下面针对MAC平台会有些特殊.其中android用到特定的包也需要做限定条件引用.

``` xml
<PropertyGroup>
    <!-- 如果是 Mac (Unix)，只保留 net8.0 和 net8.0-ios (或只留 net8.0) -->
    <TargetFrameworks Condition="$([MSBuild]::IsOSPlatform('osx'))">net8.0;net8.0-ios</TargetFrameworks>  
    <!-- 如果不是 Mac (比如 Windows)，保留全部目标 -->
    <TargetFrameworks Condition="!$([MSBuild]::IsOSPlatform('osx'))">net8.0-windows;net8.0-android;net8.0-ios;net8.0</TargetFrameworks>  
</PropertyGroup>  
<!-- 以AvaPlayer.Android启动时,AvoxControls相关的android平台需要的包引用 -->
<ItemGroup Condition="$(TargetFramework.Contains('android'))">    
    <PackageReference Include="Xamarin.AndroidX.Core.SplashScreen" />
</ItemGroup>
```

## Android

前面说了,android平台里原来C++与java的互操作,现在换成C#如何实现了?

[NdkCamera2使用OES纹理渲染](https://zhuanlan.zhihu.com/p/1967658409848440079) 这里有C++调用java的AvoxSurfaceTextureOb.java,简单来说,针对原java的类做了扩展,然后C++里调用扩展的类,在Avalonia里,需要把这些java文件以包avox.android.library建立对应目录,并把相应文件复制到对应目录.

![结构](../../assets/images/avalonia/20260106-143155.png)

修改AvaPlayer.Android.csproj文件,指明包含java文件.顺便包含链接底层的SO库文件方法.

``` xml
  <!-- 包含Java源文件 -->
  <ItemGroup>
    <AndroidJavaSource Include="$(MSBuildThisFileDirectory)Java\**\*.java" />
  </ItemGroup>
  <!-- 添加对原生库文件的引用 -->
  <ItemGroup>
  <AndroidNativeLibrary Include="$(SolutionDir)..\..\..\build\android\avplay\install\aarch64\*.so">
    <Link>lib\arm64-v8a\%(Filename)%(Extension)</Link>
  </AndroidNativeLibrary>
  </ItemGroup>
```

这样原来C++调用java对象就能正常工作了.

而java调用C++的地方,如初始化时,需要传入主activity用于后面获取AAssetManager对象.

``` java
public class JNIHelper {
    static {      
        System.loadLibrary("c++_shared");
        System.loadLibrary("avox");
    }    
    private static native void jniSetup(Activity activity);
    
    public static void initJNI(Activity activity) {
        jniSetup(activity);
    }
    public static native long getNativeSurface(Surface surface);
}
JNIEXPORT void JNICALL Java_avox_android_library_JNIHelper_jniSetup(
    JNIEnv *env, jclass clazz, jobject activity) {
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox jniSetup");
  if (activity) {
    // 必须将局部引用转换为全局引用
    aenv.activity = env->NewGlobalRef(activity);
    // 获取activity对象的class
    jclass activityClass = env->GetObjectClass(activity);
    // 转换为全局引用
    aenv.activityClass = (jclass)env->NewGlobalRef(activityClass);
    // 删除局部引用
    env->DeleteLocalRef(activityClass);
  }
  // 初始化android相关的资源
  AvoxManager::Get().initAndroid(aenv);
  // 注册AudioTrack相关方法
  getAudiotrackFields();
  // 注册AudioRecord相关方法
  getAudioRecordFields();
  // 注册SurfaceTexture相关方法
  getSurfaceTextureFields();
  // 注册Surface相关方法
  getSurfaceFields();
  // 注册AvoxSurfaceTextureOb相关方法
  getSurfaceTextureObFields();
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox jniSetup end");
}
```

现在在AvaPlayer.Android里,这种java调用C++的方式改为C#调用java就行,比我想像中方便.

``` C#
public class MainActivity : AvaloniaMainActivity<App>
{
    protected override void OnCreate(Bundle savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        // 使用JNI调用Java类的initJNI方法
        using (var jniHelperClass = Java.Lang.Class.ForName("avox.android.library.JNIHelper"))
        {
            var initJniMethod = jniHelperClass.GetMethod("initJNI", Java.Lang.Class.FromType(typeof(Activity)));
            initJniMethod.Invoke(null, this);
        }
    }
}
```

有时vs2022不能使用真机调试,但是adb devices又能看到设备,根据[设置用于调试的 Android 设备](https://learn.microsoft.com/zh-cn/dotnet/maui/android/device/setup?view=net-maui-10.0),尝试通过 Android SDK 管理器安装 Google USB 驱动程序这边可以了.

这样交互问题就解决了,原来android studio调试demo,把native加上有机率附加不上,而vs2022调试尽然没有这个问题,可以直接由C#调试到C++代码里,多次测试没有任何问题,这点很赞.

## iOS

在android里,体验到vs2022比android studio对native C++调试方便后,根据[连接到Mac以进行iOS开发](https://learn.microsoft.com/zh-cn/dotnet/maui/ios/pair-to-mac?view=net-maui-9.0)在windows配对MAC,注意二边的net-ios要一致,windows与mac使用dotnet workload list查看可以确定,然后根据[如何在iPhone或iPad上构建和运行应用程序](https://docs.avaloniaui.net/zh-Hans/docs/guides/platforms/ios/build-and-run-your-application-on-your-iphone-or-ipad)构建运行应用程序,花了些时间,终于在windows用vs2022编译ios项目搞起了,但是,相对android,几个问题,一是启动ios真机慢,二是关闭就会卡很长时间,并且与mac断开连接,后续就连不上了,需要再次重启vs2022才行,暂时看来不太实用.

根据AI建议,还是在mac安装JetBrains Rider,使用JetBrains Rider直接在MAC编译AvaPlayer.iOS,感觉还不错,调试也方便,等在MAC上解决如初始化crash,库加载不上,没正常链接等问题后,然后又切换到windows上用vs2022配对mac开发ios又正常了,不过总体来说,开发android很方便,不熟悉也能根据调试信息查找问题,而在开发IOS时,对Avalonia不熟悉时,转到MAC上开发比较好,待能正常编译运行后,后续开发再切换到windows用vs2022直接无缝切换windows/android/ios开发,就很方便了.

当前AvaPlayer.iOS.csproj如下.

``` xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net9.0-ios</TargetFramework> 
    <ValidateXcodeVersion>false</ValidateXcodeVersion> 
    <NoWarn>NU1605;NU1202;NU1213;XG0001</NoWarn>    
    <SupportedOSPlatformVersion>16.0</SupportedOSPlatformVersion>
    <UseInterpreter>true</UseInterpreter>
    <ProvisioningType>manual</ProvisioningType>
	  <CodeSignKey>Apple Development: (自己的key)</CodeSignKey>   
    <MtouchUseLlvm>false</MtouchUseLlvm> 
    <CppBuildRoot>/Volumes/PSSD/work/github/avplay/build/ios/avplay/install/aarch64</CppBuildRoot>
    <WebRTC>/Volumes/PSSD/work/webrtc/build/ios/debug/obj</WebRTC>
    <MtouchNoSymbolStrip>true</MtouchNoSymbolStrip>
    <MtouchExtraArgs>$(MtouchExtraArgs) --registrar:static -gcc_flags "-framework VideoToolbox -framework AVFoundation -framework AudioToolbox -framework CoreMedia -framework CoreVideo -framework CoreGraphics -framework UIKit -framework QuartzCore -framework CoreAudio -framework CoreMotion -framework CoreBluetooth -framework GameController -framework Metal -framework IOSurface -lz -lbz2 -liconv"</MtouchExtraArgs> 
    <!-- <MtouchNoSymbolStrip>true</MtouchNoSymbolStrip>
    <MtouchLink>None</MtouchLink>     -->
  </PropertyGroup>
  <!-- CppBuildRoot动态判定 -->
  <ItemGroup Condition="'$(OS)' != 'Windows_NT'">  
    <NativeReference Include="$(CppBuildRoot)/Debug/*.a">
      <Kind>Static</Kind>
      <ForceLoad>True</ForceLoad>
      <SmartLink>True</SmartLink>
    </NativeReference> 
    <!-- WebRTC 库通常建议 SmartLink=True，防止符号冲突 -->
    <NativeReference Include="$(WebRTC)/*.a">
      <Kind>Static</Kind>
      <ForceLoad>False</ForceLoad>
      <SmartLink>True</SmartLink>
    </NativeReference>    
    <!-- 动态库、框架和资源保持不变 -->
    <NativeReference Include="$(CppBuildRoot)/Debug/*.dylib" Kind="Dynamic" SmartLink="True" />
    <NativeReference Include="$(CppBuildRoot)/MoltenVK.framework" Kind="Framework" SmartLink="True" IsCxx="True" />    
    <BundleResource Include="$(CppBuildRoot)/avox.bundle/**">
        <Link>Resources/avox.bundle/%(RecursiveDir)%(FileName)%(Extension)</Link>
        <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
    </BundleResource>
  </ItemGroup>
  <ItemGroup>
    <PackageReference Include="Avalonia.iOS" />
    <PackageReference Include="HarfBuzzSharp" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="..\AvaPlayer\AvaPlayer.csproj" />
  </ItemGroup>
</Project>
```

使用系统的framework,在MtouchExtraArgs指定.

开始报找不到P/Invoke里的函数,主要是就是前面说的,swig把avox_csharp改成__Internal,因为是静态链接,在最后的结果上就没有avox_csharp这个C++库.

然后报webrtc相关函数找不到,因为没有链接webrtc的库,加上WebRTC的静态库链接.再次编译,ffmpeg的函数重复定义,因为webrtc与当前项目都链接了ffmpeg,重新编译webrtc,把rtc_use_h264=false就行,因为不需要webrtc内置的编解码器,用本项目封装ffmpeg实现的软解软编以及各平台的原生硬解硬编器,并把其注册到webrtc的解码编码工厂中给webrtc使用,编译各平台的webrtc的部分在这就不细说了,后面有时间看看是否记录下编译windows/android/ios/linux的webrtc m138分支的坑.

引用外部的framework,就和上面的MoltenVK.framework的方式一样.

AvaPlayer.iOS.csproj本身只在MAC上,上面为什么还加个Condition判定了,主要是用了在windows上开发IOS项目时,把CppBuildRoot的这个判定路径改为在MAC编译时成立,否则在vs2022上编译时就因为路径在windows不存在而编译不过.

## 多平台原生窗口

C++底层用vulkan/平台原生渲染SDK建立Swapchain,在windows用窗口句柄,android用ANativeWindow,ios用CAMetalLayer,Avalonia里,如何生成这些原生窗口,并把窗口句柄传给C++底层?

Avalonia本身UI是自渲染的,但是在播放器渲染视频帧时,不管是用vulkan,还是用原生的dx11/opengl/metal,都需要用原生窗口来生成Swapchain并渲染.

``` C++
// 前置声明平台原生类型
#ifdef WIN32
struct HWND__;
typedef struct HWND__* HWND;
#define AvoxSurfaceType HWND
//  win32实例 HINSTANCE
#define AvoxInstanceType void*
#endif
#ifdef __ANDROID__
struct ANativeWindow;
#define AvoxSurfaceType ANativeWindow*
struct android_app;
#define AvoxInstanceType android_app*
#endif
#ifdef __APPLE__
#ifdef __OBJC__
@class CAMetalLayer;
#else
typedef void CAMetalLayer;
#endif
#define AvoxSurfaceType CAMetalLayer*
//  ios实例 NSApplication
#define AvoxInstanceType void*
#endif
// andoroid/ios
class Window : public Observer<IWindowOb> {
  AvoxSurfaceType surface = nullptr;  
};
// vk
class VkWindow : public VkContextRef, public Window {
private:
  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  VkSwapchainKHR swapChain = VK_NULL_HANDLE;    
public:
  // 没有外部窗口,自己创建
#if _WIN32
  friend LRESULT handleMessage(HWND hWnd, UINT msg, WPARAM wparam,
                               LPARAM lparam);
  // 根据窗口创建surface,并返回使用的queueIndex.
  void initVkSurface(HINSTANCE inst, HWND windowHandle);
#endif
#ifdef __ANDROID__
  friend void handleAppCommand(android_app *app, int32_t cmd);
  void initVkSurface(ANativeWindow *window);
#endif
#ifdef __APPLE__
  void initVkSurface(CAMetalLayer *metalLayer);
#endif
#ifdef __ONLY_LINUX__ 
  void initVkSurface(IWayLandSurface *wlsurface);
#endif    
};
// dx11
class Dx11Window : public IDx11Context, public Window {
 private:
 MComPtr<IDXGISwapChain> swapChain = nullptr;
};

```

Avalonia提供一个类NativeControlHost,用来承载原生控件,利用这个类可以创建原生平台控件并把其句柄给C++底层使用.

``` C#
using Avalonia.Controls;
using Avalonia.Platform;
using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using Avalonia;

#if ANDROID
using Avalonia.Android;
using Android.Views;
using Avalonia.Android.Platform;
#endif
#if IOS
using UIKit;
using ObjCRuntime;
using CoreAnimation;
using Foundation;
using Metal;
#endif

namespace AvoxControls
{
    public class AvRenderView : NativeControlHost, IDisposable
    {
        private IntPtr platformHandle;
        private AvPlayerModel playerModel;
#if IOS
        // 防止GC
        private MetalView metalView ;
#endif
        public AvRenderView()
        {
        }

        public AvPlayerModel PlayerModel
        {
            get => playerModel;
            set
            {
                playerModel = value;
                playerModel.PropertyChanged += OnPlayerModelPropertyChanged;
            }
        }

        protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
        {
            // Windows平台创建的窗口是可以直接绘制的
            // android的NativeControl是view,绘制需要SurfaceView
            // IOS的NativeControl是UIView，绘制需要CAMetalLayer
#if ANDROID
            // parent.Handle 指向的是 Android 的 ViewGroup (Avalonia 容器)
            var platformParent = parent as AndroidViewControlHandle;
            var parentView = platformParent?.View;
            var context = parentView?.Context ?? Android.App.Application.Context;
            // SurfaceView 继承自 View,所以完全符合 NativeControlHost 的要求
            var surfaceView = new SurfaceView(context);
            // 设置回调以获取 ANativeWindow 所需的 Surface
            surfaceView.Holder?.AddCallback(new SurfaceCallback(this));
            // 返回包装后的 View 句柄
            return new AndroidViewControlHandle(surfaceView);
#elif IOS
            // 创建支持 Metal 的自定义 UIView
            metalView = new MetalView();
            // metalView.Handle 是 UIView 的句柄
            // metalView.MetalLayer.Handle 是 CAMetalLayer 的句柄
            platformHandle = metalView.MetalLayer.Handle;
            OnAttachPlatformHandle();
            // 返回包装后的 iOS 句柄
            // 在 Avalonia 中通常使用包含 UIView 的平台句柄
            return new PlatformHandle(metalView.Handle,"uiview");    
#else
            var result = base.CreateNativeControlCore(parent);
            platformHandle = result.Handle;
            OnAttachPlatformHandle();
            return result;  
#endif
        }
#if ANDROID
        private class SurfaceCallback : Java.Lang.Object, ISurfaceHolderCallback
        {
            private AvRenderView renderView;
            public SurfaceCallback(AvRenderView view)
            {
                renderView = view;
            }
            public void SurfaceCreated(ISurfaceHolder holder)
            {
                // 获取 Java 层的 Surface 对象句柄
                IntPtr surfaceHandle = holder.Surface.Handle;  
                using (var jniHelperClass = Java.Lang.Class.ForName("avox.android.library.JNIHelper"))
                {
                    var getNativeSurfaceMethod = jniHelperClass.GetMethod("getNativeSurface", 
                    Java.Lang.Class.FromType(typeof(Android.Views.Surface)));
                    var resultObj = getNativeSurfaceMethod.Invoke(null, holder.Surface);
                    if (resultObj != null)
                    {
                        renderView.platformHandle = new IntPtr((long)resultObj);
                        renderView.OnAttachPlatformHandle();
                    }
                }                
            }

            public void SurfaceChanged(Android.Views.ISurfaceHolder holder, Android.Graphics.Format format, int width, int height) { }

            public void SurfaceDestroyed(Android.Views.ISurfaceHolder holder) {       
            }
        }
#elif IOS
        public class MetalView : UIView
        {
            // 关键：告诉 UIKit 这个视图的底层 Layer 使用 CAMetalLayer
            [Export("layerClass")]
            public static Class LayerClass() => new Class(typeof(CAMetalLayer));
            public CAMetalLayer MetalLayer => (CAMetalLayer)Layer;
            public MetalView()
            {
                // 配置 Metal 层参数
                MetalLayer.Opaque = true;
                // 根据需要设置像素格式，通常是 BGRA8Unorm
                MetalLayer.PixelFormat = MTLPixelFormat.RGBA8Unorm; 
            }
            // 必须处理尺寸变化，否则渲染出来的画面可能变形或只有一角
            public override void LayoutSubviews()
            {
                base.LayoutSubviews();
                // 关键：同步 View 尺寸到 MetalLayer
                // iOS 高刷屏/Retina屏需要乘以 ContentsScale，否则画面会模糊且尺寸不对
                var scale = UIScreen.MainScreen.Scale;
                var drawableSize = new CoreGraphics.CGSize(
                    Bounds.Width * scale, 
                    Bounds.Height * scale
                );
                if (drawableSize.Width > 0 && drawableSize.Height > 0)
                {
                    MetalLayer.DrawableSize = drawableSize;
                    MetalLayer.ContentsScale = scale;
                }
            }
        }
#endif
        protected override void DestroyNativeControlCore(IPlatformHandle control)
        {
            // 解绑播放器句柄
            OnDetachPlatformHandle();
            platformHandle = IntPtr.Zero;
            base.DestroyNativeControlCore(control);
        }
        
        protected void OnAttachPlatformHandle()
        {
            if (PlayerModel?.Player != null && platformHandle != IntPtr.Zero)
            {
                ISurfaceRender winRender = PlayerModel.Player.GetSurfaceRender();
                if (winRender != null)
                {
                    winRender.setSurface(platformHandle, ConfigManager.Instance.Config.UseVulkanRendering);
                }
            }
        }

        protected void OnDetachPlatformHandle()
        {
            if (PlayerModel?.Player != null)
            {
                ISurfaceRender winRender = PlayerModel.Player.GetSurfaceRender();
                if (winRender != null)
                {
                    winRender.setSurface(IntPtr.Zero, ConfigManager.Instance.Config.UseVulkanRendering);
                }
            }
        }

        private void OnPlayerModelPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == "Player" && platformHandle != IntPtr.Zero)
            {
                OnAttachPlatformHandle();
            }
        }

        public void Dispose()
        {
            // 移除事件监听
            if (playerModel is INotifyPropertyChanged notifyPropertyChanged)
            {
                notifyPropertyChanged.PropertyChanged -= OnPlayerModelPropertyChanged;
            }
            // 释放平台资源
            OnDetachPlatformHandle();
        }
    }
}
```

这样就可以把各原生控件显示在Avalonia中了.

到此,主要想到的可能技术问题差不多都解决了,后面就是让AI设计界面并完成逻辑了.

