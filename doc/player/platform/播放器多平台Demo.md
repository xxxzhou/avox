# 播放器多平台Demo

当前项目对应的多平台SDK，由此UI是要满足各种语言的，能被各式项目集成，包含UE,unity,winform,nodejs,android/ios原生界面这些。

调用项目本身是C++的项目，就不说了，直接通过头文件/库调用就行，说几种夸语言调用方案实现。

如下有很多CMake代码，说实话是我认为的难点，因为要结合项目，当项目本身改变后，人尽量少插手，让程序在编译时根据项目变更全自动更新所有封装到别的语言实现，在Cmake编译构建时自动完成相应封装。

## Windows平台通过C#调用

![window_demo](../../assets/images/media/window_demo.png)

平常功能测试与调试就使用平台对应的原生窗口，需要UI测试的功能，我选择winform，至于为啥不用同为C++的界面方案QT或是MFC，因为对我来说，封装C++给C#，然后使用Winform生成界面然后测试与调试功能，比QT或是MFC方便要方便的多，请看我的实现。

如下是截取播放器部分给外部项目调用的C++头文件实现。

``` C++
class IMediaPlayerOb {
public:
  IMediaPlayerOb() = default;
  virtual ~IMediaPlayerOb() = default;

public:
  virtual void onStateChange(PlayerState preState, PlayerState state) {}
  virtual void onIoError(IoError error) {}
  virtual void onDecodeError(TrackType trackType, DecodeResult error) {}

  // 拿到流信息
  virtual void onReady() {}
  virtual void onComplete() {}
  virtual void onSeek() {}
  virtual void onPause() {}
  virtual void onResume() {}
  virtual void onClose() {}
};

// 播放器
class IMediaPlayer {
public:
  IMediaPlayer() = default;
  virtual ~IMediaPlayer() = default;

public:
  // 返回内置的参数设置器
  virtual IOption *getOption() = 0;
  // 播放器埋点信息返回
  virtual void setPingbackOb(IPingbackOb *ob) = 0;
  // 这个选项确定使用的IO方案(ffmpeg/zlmediakit),下次打开启用
  virtual void setIoPlan(IoPlan plan) = 0;
  // 这个选项确定视频是否硬解
  virtual void setHardDecode(bool hard) = 0;
  // 这个选项确定视频使用的渲染方案
  virtual void setVRenderType(RenderType type) = 0;
  // 这个选项确定音频使用的渲染方案
  virtual void setARenderType(ARenderType type) = 0;
  virtual void setWindow(IWindow *window, int32_t vtrack = 0) = 0;
  virtual void open(const char *url) = 0;
  virtual void stop() = 0;
  virtual void seek(int64_t pos) = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void speed(double speed) = 0;

  virtual PlayerState getState() = 0;
  virtual double getProcess() = 0;
  virtual int64_t getDuration() = 0;
  virtual int64_t getPosition() = 0;

  // 需要状态在ready之后才能调用(建议IMediaPlayerOb里的onReady回调里调用)
  virtual int32_t videoTrackSize() = 0;
  virtual IVideoTrack *getVideoTrack(int32_t index = 0) = 0;
  virtual int32_t audioTrackSize() = 0;
  virtual IAudioTrack *getAudioTrack(int32_t index = 0) = 0;
};

extern "C" {
AVOX_EXPORT IMediaPlayer *createMediaPlayer();
AVOX_EXPORT void addMediaPlayerOb(IMediaPlayer *player, IMediaPlayerOb *ob);
AVOX_EXPORT void removeMediaPlayerOb(IMediaPlayer *player, IMediaPlayerOb *ob);
AVOX_EXPORT const char *getIoPlanStr(IoPlan plan);
AVOX_EXPORT const char *getIoErrorStr(IoError error);
AVOX_EXPORT const char *getPlayerStateStr(PlayerState state);
AVOX_EXPORT const char *getTrackTypeStr(TrackType type);
AVOX_EXPORT const char *getDecodeResultStr(DecodeResult error);
AVOX_EXPORT const char *getARenderTypeStr(ARenderType type);
}
```

直接手动封装麻烦，并且如果有改动，不同平台不同语言都要改，后续维护也麻烦，所以我考虑通过swig来实现，这样做的好处是，不同平台不同语言的项目，都可以通过swig生成对应的文件，结合cmake,每次改动在编译时就自动更新生成新的接口封装。

如下是swig生成C#相应的cmake封装,首先是生成封装C++项目代码，这个项目是C++/C#对接的C++项目avox_csharp。

``` cmake
if(AVOX_ENABLE_SWIG_CSHARP AND WIN32) 
    message(STATUS "SWIG C# enabled, generating C# bindings...")
    # 添加宏定义 
    list(APPEND SWIG_DEFINITIONS -DAVOX_WIN32) 
    # 设置输出目录
    set(CMAKE_SWIG_OUTDIR "${CMAKE_CURRENT_SOURCE_DIR}/csharp/files") 
    # 清理旧文件（新增部分）
    # execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SWIG_OUTDIR})
    # 配置 SWIG 参数    
    set_source_files_properties(${WRAPPERLIST} PROPERTIES CPLUSPLUS ON CMAKE_SWIG_FLAGS "-includeall")
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -namespace AvoxNet)
    # 生成 SWIG 包装代码
    swig_add_library(avox_csharp LANGUAGE csharp SOURCES ${WRAPPERLIST})
    swig_link_libraries(avox_csharp avox)
    avox_output(avox_csharp) 
    if(EXISTS "${CMAKE_SWIG_OUTDIR}") 
        add_subdirectory(csharp) 
    endif()
endif()
```

把生成的C#项目封装打包，这个项目是C++/C#对接的C#项目AvoxSharp，可以直接在C#项目里引用。

``` cmake
# 把.cs文件生成C#dll
project(AvoxSharp VERSION 0.1.0 LANGUAGES CSharp)

include(CSharpUtilities)
include(AVOXHelper) 

# # wrapper文件夹里文件有改动,需要刷新当前CMakeLists.txt
file(GLOB SHARP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/files/*.cs")    
# message(STATUS "SHARP_FILES: " ${SHARP_FILES})  
add_library(AvoxSharp SHARED ${SHARP_FILES})       
set_property(TARGET AvoxSharp PROPERTY DOTNET_TARGET_FRAMEWORK_VERSION "v4.7.2")  
# debug/release,开启C#本地代码调试
if(AVOX_DEBUG)  
    set_property(TARGET AvoxSharp PROPERTY VS_GLOBAL_EnableUnmanagedDebugging "true")          
endif()      
# 64/32位 
if(AVOX_PLATFORM_X64)                         
    set_property(TARGET AvoxSharp PROPERTY WIN32_EXECUTABLE FALSE)        
else() 
    set_property(TARGET AvoxSharp PROPERTY WIN32_EXECUTABLE TRUE)         
endif() 
avox_output(AvoxSharp)    

if(EXISTS "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll")
    file(COPY "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll" 
         DESTINATION "${AVOX_PLATFORM_PATH}")
else()
    message(WARNING "源文件不存在: ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll") 
endif()

if(EXISTS "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll")
    file(COPY "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll" 
         DESTINATION "${AVOX_PLATFORM_PATH}")
else()
    message(WARNING "源文件不存在: ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll")
endif()
```

可以看下C#如何调用封装后的代码。

``` C#
internal class MediaPlayer
{
    public class MediaPlayerOb : IMediaPlayerOb
    {
        // 持有父类实例的引用
        private readonly MediaPlayer parent;
        // 通过构造函数注入父类实例
        public MediaPlayerOb(MediaPlayer parent_)
        {
            parent = parent_;
        }
        public override void onReady()
        {
            if (parent.Player.videoTrackSize() > 0)
            {
                parent.VideoTrack = parent.Player.getVideoTrack();
            }
            if (parent.Player.audioTrackSize() > 0)
            {
                parent.AudioTrack = parent.Player.getAudioTrack();
            }
        }
        public override void onClose()
        {
            parent.VideoTrack = null; 
            parent.AudioTrack = null;
        }
    }
    // 父类中创建子类实例
    private readonly MediaPlayerOb observer;    
    public MediaPlayer()
    {
        observer = new MediaPlayerOb(this);
        Player = AvoxWrapper.createMediaPlayer();
        AvoxWrapper.addMediaPlayerOb(Player, observer);
    }
    public IAudioTrack? AudioTrack { private set; get; }
    public IVideoTrack? VideoTrack { private set; get; }
    public IMediaPlayer Player { private set; get; }
}
public partial class Form1 : Form
{
    // 导入 kernel32.dll 的 GetModuleHandle 函数
    [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
    public static extern IntPtr GetModuleHandle(string lpModuleName);
    private MediaPlayer mediaPlayer;
    private IWindow window;
    private Bitmap bimMap;
    private readonly WinLog winLog = new();
    private bool bPause = false;
    public Form1()
    {
        InitializeComponent();
        AvoxWrapper.setLogObserver(winLog);
        pictureBox1.SizeChanged += PictureBox1_SizeChanged;
        timer1.Tick += Timer1_Tick;
        timer1.Enabled = true;
        Console.WriteLine("form");
    }
    private void Timer1_Tick(object? sender, EventArgs e)
    {
        if (mediaPlayer == null)
        {
            return;
        }
        //Console.WriteLine(string.Format("now time:{0},total time:{1}",
        //    mediaPlayer.getPosition(),
        //    mediaPlayer.getDuration()));
        double process = mediaPlayer.Player.getProcess();
        progressBar1.Value = (int)(process * 100);
        label2.Text = AvoxWrapper.getPlayerStateStr(mediaPlayer.Player.getState());
        label4.Text = mediaPlayer.AudioTrack?.getRate().ToString();
        label6.Text = mediaPlayer.VideoTrack?.getRate().ToString();
    }
    private void PictureBox1_SizeChanged(object? sender, EventArgs e)
    {
        mediaPlayer.Player.setWindow(window);
    }
    private void Form1_Load(object sender, EventArgs e)
    {
        mediaPlayer = new MediaPlayer();
        window = AvoxWrapper.createVkWindow();
        WindowParamet wp = new WindowParamet();
        wp.width = pictureBox1.ClientSize.Width;
        wp.height = pictureBox1.ClientSize.Height;
        wp.handle = pictureBox1.Handle;
        wp.instance = GetModuleHandle(null);
        window.initWindow(wp);
        mediaPlayer.Player.setWindow(window);
        textBox1.Text = "rtsp://127.0.0.1:554/live/test";
        window.run(true);
    }
    private void button1_Click(object sender, EventArgs e)
    {
        DialogResult dialogResult = openFileDialog1.ShowDialog();
        if (dialogResult == DialogResult.Cancel)
        {
            return;
        }
        mediaPlayer.Player.open(openFileDialog1.FileName);
        
    }
    private void button2_Click(object sender, EventArgs e)
    {
        mediaPlayer.Player.open(textBox1.Text);         
    }
    private void button3_Click(object sender, EventArgs e)
    {
        bPause = !bPause;
        if (bPause)
        {
            mediaPlayer.Player.pause();
        }
        else
        {
            mediaPlayer.Player.resume();
        }
    }
    private void button4_Click(object sender, EventArgs e)
    {
        mediaPlayer.Player.stop();
    }
    private void progressBar1_MouseClick(object sender, MouseEventArgs e)
    {
        // 确保进度条可操作
        if (mediaPlayer == null)
        {
            return;
        }
        // 计算点击位置对应的 Value 值
        double ratio = (double)e.X / progressBar1.Width;
        int newValue = (int)(ratio * (progressBar1.Maximum - progressBar1.Minimum)) + progressBar1.Minimum;
        // 边界保护
        newValue = Math.Clamp(newValue, progressBar1.Minimum, progressBar1.Maximum);
        // 更新进度条
        progressBar1.Value = newValue;
        long seekTime = newValue * mediaPlayer.Player.getDuration() / 100;
        mediaPlayer.Player.seek(seekTime);
    }
    private void trackBar1_ValueChanged(object sender, EventArgs e)
    {
        if (mediaPlayer == null)
        {
            return;
        }
        Int32 speedV = trackBar1.Value;
        double speed = (double)speedV / 10;
        mediaPlayer.Player.speed(speed);
        label7.Text = "速度:" + speed;
    }
}
```

## Windows平台通过JS调用

现在界面越来越多通过前端JS调用，这种方式界面好做，这边通过使用Node提供的js/C++的addon来实现。还有另外一种尝试，使用wasm,需要把库及第三库全使用wasm的方式编译，以及线程方式的限制，后面考虑这种方式。

分为二步，首先通过swig把C++接口生成node C++插件格式的js文件。

``` cmake
if(AVOX_ENABLE_SWIG_NODEJS AND WIN32)
    message(STATUS "SWIG Node.js enabled, generating Node.js bindings...")
    # 添加宏定义 
    list(APPEND SWIG_DEFINITIONS -DAVOX_NODEJS) 
    # 设置Node.js模块输出目录
    set(NODEJS_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nodejs/files") 
    set(CMAKE_SWIG_OUTDIR ${NODEJS_OUTPUT_DIR})    
    # 配置SWIG参数
    set_source_files_properties(${WRAPPERLIST} PROPERTIES USE_TARGET_INCLUDE_DIRECTORIES ON PLUSPLUS ON)       
    # 清空之前的CMAKE_SWIG_FLAGS
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -node)    
    # 使用add_custom_command生成Node.js原生扩展
    add_custom_command(
        OUTPUT ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx
        COMMAND ${SWIG_EXECUTABLE} -javascript -node -c++ -I"${CMAKE_SOURCE_DIR}/src" -o ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx ${WRAPPERLIST}
        DEPENDS ${WRAPPERLIST}
        COMMENT "Generating SWIG wrapper for Node.js"
    )
    add_custom_target(avox_nodejs_swig ALL DEPENDS ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx)
endif()
```

然后把生成的JAVASCRIPT_wrap.cxx使用node-gyp生成可供JS使用的本地插件。

相应使用流程：

1. 安装nodejs,可以的话，直接在安装VS2022时选择包含node.生成目录在比如C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs。
2. 安装node-gyp,如果node在C盘，使用管理员启动CMD, npm install -g node-gyp。生成目录在C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs\node_modules\node-gyp。其目录下addon.gypi包含头文件路径引用。node-gyp安装时会安装一个带v8头文件的node,如C:\Users\UsersName\AppData\Local\node-gyp\Cache\20.13.1。
3. CMD定位到当前包含binding.gpy目录下，使用node-gyp configure生成项目，根据提示确定是否需要安装nan/node-addon-api，npm install nan/npm install node-addon-api。
4. 配置后在当前的build目录下，有一个config.gypi,其中nodedir会指定上面的node-gyp安装的node目录。
5. 使用node-gyp build编译项目。
6. 使用BuildType/avox_js.node。

然后使用js测试。

``` javascript
const path = require('path')
const util = require('util');
const avox = require('./build/Release/avox_js.node')

const avoxjs = Object.keys(avox)
// 查看avox里所有方法
// console.log(util.inspect(avox, {showHidden: false, depth: null}));
// 查看avox里所有方法
// console.log(Object.keys(avox));
// console.dir(avox, {depth: null})

avox.logMsg(1,"hello world");
console.log("io plan:",avox.IoPlan_ffmpeg)
// 将返回值赋给字符串变量
const planStr = avox.getIoPlanStr(avox.IoPlan_ffmpeg); 
// 打印出来查看
console.log('IO Plan字符串:', planStr); 
```

js返回与C++同样的输出，暂时还没完成vulkan渲染到浏览器窗口的功能，因为这部分demo还没有，但是这种试测试多线程相关的调用都已正常。

## Android平台通过java调用

![android_demo](../../assets/images/media/android_demo.png)

和上面一样，先通过swig把C++头文件生成对应的java接口,最开始是直接把生成的java文件复制到java封装模块目录下，后续为了方便给上层调用，改为把生成的java文件列表直接在cmake里打成jar包，然后让java封装模块引用这个jar包。

``` cmake
if(AVOX_ENABLE_SWIG_JAVA AND ANDROID)
    message(STATUS "SWIG Java enabled, generating Java bindings...")
    # 添加宏定义
    list(APPEND SWIG_DEFINITIONS -DAVOX_ANDROID)
    include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
    # 设置Java包名及对应目录结构
    set(AVOX_JAVA_PACKAGE "avox.android.library.swig")
    string(REPLACE "." "/" AVOX_JAVA_SUBDIR ${AVOX_JAVA_PACKAGE})
    # 设置SWIG生成Java文件的临时输出目录
    set(AVOX_SWIG_TEMP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/java/files)
    set(CMAKE_SWIG_OUTDIR ${AVOX_SWIG_TEMP_DIR}/${AVOX_JAVA_SUBDIR})
    message(STATUS "SWIG Java output directory: ${CMAKE_SWIG_OUTDIR}")
    # 清理旧的输出目录并创建新目录
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SWIG_OUTDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SWIG_OUTDIR})
    # 设置SWIG参数和属性
    set_source_files_properties(${WRAPPERLIST} PROPERTIES CPLUSPLUS ON)
    set(CMAKE_SWIG_FLAGS -c++ -package ${AVOX_JAVA_PACKAGE} -O ${SWIG_DEFINITIONS})
    # 生成SWIG包装库
    swig_add_library(avox_java LANGUAGE java SOURCES ${WRAPPERLIST})
    swig_link_libraries(avox_java avox)
    # 设置Java编译输出目录和jar包路径
    set(JAVA_CLASSES_DIR ${CMAKE_BINARY_DIR}/java_classes)
    set(JAR_FILE ${CMAKE_CURRENT_SOURCE_DIR}/java/avox_swig.jar)
    # 确保Java类输出目录存在
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${JAVA_CLASSES_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${JAVA_CLASSES_DIR})
    # 构建阶段：生成Java文件列表并编译成class文件（改用CMake脚本生成文件列表）
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Generating sources.txt for Java compilation"
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/generate_sources.cmake
        COMMAND cmd /c "javac -d ${JAVA_CLASSES_DIR} @${CMAKE_SWIG_OUTDIR}/sources.txt"
        WORKING_DIRECTORY ${CMAKE_SWIG_OUTDIR}
        COMMENT "Compiling SWIG generated Java sources"
        VERBATIM)
    # 生成辅助CMake脚本generate_sources.cmake，内容示例：
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generate_sources.cmake
    "file(GLOB_RECURSE JAVA_FILES RELATIVE \"${CMAKE_SWIG_OUTDIR}\" \"*.java\")\n"
    "file(WRITE \"${CMAKE_SWIG_OUTDIR}/sources.txt\" \"\")\n"
    "foreach(file IN LISTS JAVA_FILES)\n"
    "  file(APPEND \"${CMAKE_SWIG_OUTDIR}/sources.txt\" \"${CMAKE_SWIG_OUTDIR}/\${file}\\n\")\n"
    "endforeach()\n")
    # 构建阶段：打包class文件成jar包
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND jar cf ${JAR_FILE} -C ${JAVA_CLASSES_DIR} .
        COMMENT "Packaging SWIG generated Java classes into jar"    )
    # 构建阶段：复制jar包到指定目录
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying SWIG jar to AvoxJava/avox/libs"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/AvoxJava/avox/libs
        COMMAND ${CMAKE_COMMAND} -E copy ${JAR_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/AvoxJava/avox/libs/
        COMMENT "Ensure libs directory exists and copy SWIG jar"
    )
    # 构建阶段：复制生成的JNI库到安装目录
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying Java native library..."
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:avox_java> ${CMAKE_INSTALL_PREFIX}
        COMMENT "Copying Java JNI library to runtime directory"
    )
endif()
```

java封装模块引用这个avox_swig.jar模块，调用并测试代码。

``` java
public class MediaPlayer {

    private MediaPlayerOb observer = null;
    public IMediaPlayer Player = null;
    public IAudioTrack AudioTrack = null;
    public IVideoTrack VideoTrack = null;

    public static class MediaPlayerOb extends IMediaPlayerOb {
        private MediaPlayer parent = null;

        public MediaPlayerOb(MediaPlayer parent_) {
            parent = parent_;
        }

        @Override
        public void onReady() {
            if (parent.Player.videoTrackSize() > 0) {
                parent.VideoTrack = parent.Player.getVideoTrack();
            }
            if (parent.Player.audioTrackSize() > 0) {
                parent.AudioTrack = parent.Player.getAudioTrack();
            }
        }

        @Override
        public void onClose(){
            // 这二值在此回调后，在C++层可能无效，在这置空保持和底层同样状态
            parent.VideoTrack = null;
            parent.AudioTrack = null;
        }
    }

    public MediaPlayer(){
        observer = new MediaPlayerOb(this);
        Player = AvoxWrapper.createMediaPlayer();
        AvoxWrapper.addMediaPlayerOb(Player,observer);
    }


    public void Open(String Uri){
        Player.open(Uri);
    }

    public void Stop(){
        Player.stop();
    }
}
// 硬解的OpenGL直接渲染窗口
public class GLVideoRender implements SurfaceHolder.Callback{
    public IWindow Window = null;
    private IGLRenderObserver copyTexture = null;

    public void init(SurfaceView surface){
        surface.getHolder().addCallback(this);
        Window = AvoxWrapper.createEglWindow();
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        // Log.i("avox", "surfaceCreated: create");
        JNIHelper.initEglWindow(Window,0,surfaceHolder.getSurface());
        Window.run(true);
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder,int format, int width, int height) {
        Log.i("avox", "surfaceChanged: width:"+width+" height:"+height);

        ImageFormat iformat = new ImageFormat();
        iformat.setWidth(width);
        iformat.setHeight(height);
        iformat.setImageType(ImageType.other);
        Window.updateSize(width,height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        Log.i("avox", "surfaceDestroyed");
        Window.close();
    }
}
public class VkVideoRender implements SurfaceHolder.Callback{
    public IWindow Window = null;
    public void init(SurfaceView surface){
        surface.getHolder().addCallback(this);
        Window = AvoxWrapper.createVkWindow();
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder,int format, int width, int height) {
        Log.i("avox", "surfaceChanged: width:"+width+" height:"+height);
        JNIHelper.initVkWindow(Window,surfaceHolder.getSurface(),width,height);
        Log.i("avox", "surfaceCreated: create");
        Window.run(true);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        Log.i("avox", "surfaceDestroyed");
        Window.close();
    }
}
public class EglActivity extends FragmentActivity implements IGLRenderObserver, View.OnClickListener{
    private GLVideoRender videoRender = null;
    private MediaPlayer mediaPlayer = null;
    private Button btnOpen = null;
    private Button btnClose = null;
    private EditText uri = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main2);

        btnOpen = findViewById(R.id.btnJoin);
        btnOpen.setOnClickListener(this);
        uri = findViewById(R.id.roomName);

        btnClose = findViewById(R.id.btnClose);
        btnClose.setOnClickListener(this);

        JNIHelper.initJNI(this);
        mediaPlayer = new MediaPlayer();

        videoRender = new GLVideoRender();
        SurfaceView glSurfaceView = findViewById(R.id.gl_surface_view);
        videoRender.init(glSurfaceView);
    }

    @Override
    public void onClick(View v) {
        // 通过View ID进行按钮区分
        if (v.getId() == R.id.btnJoin) { // btnOpen的点击处理
            IoPlan ioPlan = IoPlan.swigToEnum(SettingsManager.getIoParserType(this));
            mediaPlayer.Player.setIoPlan(ioPlan);
            // 只支持硬解
            mediaPlayer.Player.setHardDecode(true);
            mediaPlayer.Player.setVRenderType(RenderType.OpenGLES);
            mediaPlayer.Player.setWindow(videoRender.Window);
            mediaPlayer.Open(uri.getText().toString());
        }
        else if (v.getId() == R.id.btnClose) { // btnClose的点击处理
            // 添加关闭播放器的逻辑
            mediaPlayer.Player.stop();
        }
    }

    @Override
    public void renderTex(IRenderContext glesContext) {
        if(mediaPlayer.VideoTrack != null){
            // Vk输出到HarderBuffer,HarderBuffer通过glEGLImageTargetTexture2DOES到纹理
            AvoxWrapper.renderTrack(mediaPlayer.VideoTrack,glesContext);
        }
    }

    @Override
    public void onBackPressed() {
        // 先执行播放器关闭
        if (mediaPlayer != null) {
            mediaPlayer.Stop();
        }
        super.onBackPressed();
    }
}
public class VkActivity extends FragmentActivity implements View.OnClickListener {
    private VkVideoRender vkVideoRender = null;
    private MediaPlayer mediaPlayer = null;
    private Button btnOpen = null;
    private EditText uri = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnOpen = findViewById(R.id.btnJoin);
        btnOpen.setOnClickListener(this);
        uri = findViewById(R.id.roomName);

        JNIHelper.initJNI(this);

        mediaPlayer = new MediaPlayer();

        SurfaceView surfaceView = findViewById(R.id.vk_surface_view);
        vkVideoRender = new VkVideoRender();
        vkVideoRender.init(surfaceView);
    }

    @Override
    public void onClick(View view) {
        IoPlan ioPlan = IoPlan.swigToEnum(SettingsManager.getIoParserType(this));
        mediaPlayer.Player.setIoPlan(ioPlan);
        mediaPlayer.Player.setHardDecode(SettingsManager.getHardwareDecode(this));
        mediaPlayer.Player.setVRenderType(RenderType.Vulkan);
        mediaPlayer.Player.setWindow(vkVideoRender.Window);
        // openUri(uri.getText().toString());
        mediaPlayer.Open(uri.getText().toString());
    }

    @Override
    public void onBackPressed() {
        // 先执行播放器关闭
        if (mediaPlayer != null) {
            mediaPlayer.Stop();
        }
        super.onBackPressed();
    }
}
```

## IOS平台

![ios_demo](../../assets/images/media/ios_demo.png)

IOS平台比较特殊，因为obj-c可以直接调用C++代码，就和普通C++项目如UE调用一样。

``` C++
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
    if ([scene isKindOfClass:UIWindowScene.class]) {
        UIWindowScene *windowScene = (UIWindowScene *)scene;
        // 从 storyboard 获取 window
        UIStoryboard *mainStoryboard = [UIStoryboard storyboardWithName:@"Main" bundle:nil];
        self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
        self.window.rootViewController = [mainStoryboard instantiateInitialViewController];
        [self.window makeKeyAndVisible];
        
        // bool bMetalRender = false;
        bool bMetalRender = true;
        
        WindowParamet param = {};
        if ([windowScene.delegate isKindOfClass:SceneDelegate.class]) {
            ViewController *viewController = (ViewController *)self.window.rootViewController;
            if (viewController.metalView) {
                NSLog(@"Got MetalView in main.mm");
                // 获取 metalLayer
                CAMetalLayer *metalLayer = viewController.metalView.metalLayer;
                param.handle = (__bridge void *)metalLayer;

                // 获取 metalLayer 的宽度和高度
                CGRect bounds = metalLayer.bounds;
                param.width = static_cast<int>(bounds.size.width);
                param.height = static_cast<int>(bounds.size.height);

                NSLog(@"MetalLayer width: %d, height: %d", param.width, param.height);
            }
        }
        IWindow* window = nullptr;
        if(bMetalRender){
            window = createMetalWindow();
        }else{
            window = createVkWindow();
        }
        window->initWindow(param);
        // 创建me
        IMediaPlayer* mp = avox::createMediaPlayer();
        if(bMetalRender){
            mp->setVRenderType(RenderType::Metal);
        }
        // mp->open("rtsp://192.168.1.100/live/test");
        mp->open("rtsp://192.168.1.100:554/live/0123456789ab_0");
        // mp->open("rtsp://192.168.1.100:554/live/0123456789cd_0");
        mp->setWindow(window);
        window->run(true);
    }
}
```

但是如何让别的项目调用你的模块就有些麻烦，首先尽量不要使用动态库，IOS里动态库调用要求比较严格，避免麻烦，直接把需要链接的库全改为静态库，如下是我这边zlmediakit这些由gitsubmodule引用的项目，全改为静态库。

``` python
import os
import subprocess
import build_common

# 明确指定目标系统
build_common.AVOX_TARGET_SYSTEM = "ios"
# 指定架构（可根据需求修改，这里以 arm64 为例，适用于真机；x86_64 适用于模拟器）
build_common.AVOX_TARGET_ARCH = "arm64"
# 指定构建类型（Debug 或 Release）
build_common.AVOX_BUILD_TYPE = "Debug"
# vscode里改C++代码，在脚本里编译，需要强制重新编译才能应用改动代码
build_common.AVOX_FORCE_REBUILD = True
# 是否只构建项目，不编译
onlyMake = False

# -DARCHS=arm64  -DIOS_PLATFORM=OS
ZL_CMAKE_ARGS = "-DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_SERVER=OFF -DENABLE_OPENSSL=OFF -DENABLE_SRT=OFF"

if __name__ == "__main__":
    # module可以只编译一次，有改动再编译
    build_common.build_module("zlmediakit",onlyMake,ZL_CMAKE_ARGS)
    build_common.build_self(True)
```

如果xcode demo用来调试，直接引用播放器模块，并需要把CMake里引用的模块全加到xcode模块中。

``` cmake
  # avox_apple模块需要的框架列表
  set(COMMON_FRAMEWORKS Foundation UIKit GLKit OpenGLES)

  if(AVOX_ENABLE_FFMPEG)
    avox_list_append_unique(COMMON_FRAMEWORKS AVFoundation CoreGraphics CoreMedia VideoToolbox AudioToolbox CoreVideo z bz2 iconv)
  endif()

  if(AVOX_ENABLE_ZLMEDIAKIT)
    avox_list_append_unique(COMMON_FRAMEWORKS Security CoreFoundation CFNetwork)
  endif()
```

上面就一个一个加，当然很麻烦，特别是别人引用你项目的人，有一种方式你封装成framework,只需要在framework里做次封装，把相关的库引用与头文件做好引用，提供给外部封装库，还有一种比较常见的方式，就是用POD，简单来说，描述你当前项目所引用的框架以及资源，别的模块通过POD方式添加，也很方便，不用怎么封装。如下是我当前项目的POD的描述。

``` txt
Pod::Spec.new do |s|
  # 基本信息
  s.name         = "avox"
  # 动态获取版本号，可从文件或环境变量读取
  s.version      = "1.0.0" 
  s.summary      = "A summary of avox library."
  s.description  = "A detailed description of avox library."
  s.homepage     = "https://example.com"
  s.license      = { :type => "MIT", :file => "LICENSE" }
  s.author       = { "Your Name" => "your_email@example.com" }   
  # 代码签名信息  
  development_team_id = 'XXXXXXXXX'
  s.user_target_xcconfig = {
    'DEVELOPMENT_TEAM' => development_team_id,
    'CODE_SIGN_IDENTITY' => 'iPhone Developer',
    'CODE_SIGN_STYLE' => 'Automatic'    
  }

  # 支持的平台和最低版本
  s.platform     = :ios, "12.0"   
  s.source           = { :git => "", :tag => s.version.to_s } 
  # 源文件路径
  s.source_files = 'build/ios/avox/install/**/*.{h,m,cpp}' 
  # 头文件路径
  s.public_header_files = 'build/ios/avox/install/include/**/*.h'
  # 指定头文件映射目录，保持原有目录结构
  s.header_mappings_dir = 'build/ios/avox/install/include'
  # 库文件路径
  s.vendored_libraries = 'build/ios/avox/install/aarch64/Debug/*.{a,dylib}'
  # moltenvk 框架路径
  s.vendored_frameworks = 'build/ios/avox/install/aarch64/MoltenVK.framework'
  # 系统框架依赖
  s.frameworks = 'Security', 'CoreFoundation', 'CFNetwork', 'GLKit', 'OpenGLES', 'CoreMedia', 'CoreVideo', 'CoreAudio', 'AVFoundation', 'CoreGraphics', 'VideoToolbox', 'AudioToolbox','Foundation', 'CoreMotion', 'UIKit', 'QuartzCore','CoreBluetooth','GameController'
  s.libraries='z','bz2','iconv'
  # 编译选项
  s.requires_arc = true
  # 添加资源文件
  s.resources = 'build/ios/avox/install/aarch64/avox.bundle'
  # 添加其他依赖项，根据实际情况修改
  # s.dependency 'avox'
end
```

需要注意，因为是静态链接，所以zlmediakit所需要导致的frameworks/libraries,也需要在这最后封装描述里的全写入，上面的s.frameworks/s.libraries就是对应cmake里引用模块时需要引用的framework,这样后面通过POD安装就会自动引入。


