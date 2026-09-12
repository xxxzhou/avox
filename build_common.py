import os
import platform
import shutil
import subprocess
import sys

# Ensure stdout uses UTF-8 encoding for Chinese characters on Windows
if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

# 支持环境变量覆盖
# 可设置为 Debug/Release; 默认 Release 不带符号(产物体积小, 见 doc/build/构建.md), 需要调试时置 Debug
_default_build_type = os.environ.get("AVOX_BUILD_TYPE", "Release")
AVOX_BUILD_TYPE = _default_build_type   
# windows下可编译android,一个是host,一个是target
# windows/android/linux/ios/macos
AVOX_TARGET_SYSTEM = "windows" 
# target对应的cpu架构，windows下可编译x86_64,linux下可编译x86_64/arm64-v8a
# x64/arm64-v8a/arm64/x86_64（iOS 真机 arm64，模拟器 x86_64；macOS arm64/x64/universal）
AVOX_TARGET_ARCH = "x64"
# 是否强制重新构建，默认False,True会删除build目录下的所有文件
AVOX_FORCE_REBUILD = False

# NDK目录，默认对应环境变量NDK_ROOT 21.0.6113669 25.0.8775105 26.1.10909125
# 硬编需要26
AVOX_NDK_ROOT = "C:/Users/mfjt5/AppData/Local/Android/Sdk/ndk/26.1.10909125"
# windows下需要设置，非windows可以忽略; 优先环境变量, 其次按候选探测(旧机器硬编/本机LOCALAPPDATA junction)
AVOX_NINJA_PATH = os.environ.get("AVOX_NINJA_PATH", "")
if not AVOX_NINJA_PATH:
    for _cand in (
        "C:/Users/mfjt5/AppData/Local/Android/Sdk/cmake/4.0.2/bin/ninja.exe",
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk", "cmake", "4.0.2", "bin", "ninja.exe"),
    ):
        if os.path.exists(_cand):
            AVOX_NINJA_PATH = _cand
            break
AVOX_NDK_PLATFORM= "android-26"
AVOX_NDK_ARCH_LIST = ["arm64-v8a","armeabi-v7a"]

# windows
AVOX_WIN_VS_VERSION = "Visual Studio 17 2022"
AVOX_WIN_VS_ARCH_LIST = ["x64", "x86"]

# iOS 部署目标版本
AVOX_IOS_DEPLOYMENT_TARGET = "15.0" 
# iOS 支持的架构，arm64 为真机，x86_64 为模拟器
AVOX_IOS_ARCH_LIST = ["arm64"]  

# macOS 部署目标版本(arm64 下限 11.0)
AVOX_MACOS_DEPLOYMENT_TARGET = "11.0"


def get_current_target():
    return AVOX_TARGET_SYSTEM

def get_host_system():
    """
    获取当前运行的系统名称，返回值可能为'windows'、'linux'、'darwin'（对应Mac OS）或者其他特定自定义的系统名，如'android'等。
    """
    system_name = platform.system().lower()
    if system_name == "windows":
        return "windows"
    elif system_name == "linux":
        return "linux"
    elif system_name == "darwin":
        return "macos"
    else:
        return system_name
    
def get_build_path(target_name):    
    # 获取当前脚本文件的上两级目录作为项目根目录
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    # 创建一个用于存放构建文件的目录，格式为build/生成平台，例如 "build/windows"
    build_dir = os.path.join(project_root, "build", target_name)
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)    
    # print("构建目录:"+build_dir)
    return build_dir

def _cached_dist_flavor(cmake_cache):
    """读CMakeCache里生效的AVOX_DIST_FLAVOR(无条目视为agpl, flavor机制引入前的旧缓存语义)"""
    try:
        with open(cmake_cache, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("AVOX_DIST_FLAVOR:STRING="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return "agpl"

def _requested_dist_flavor(build_args):
    """从本次cmake参数解析请求的发行渠道(未传为默认commercial, 可商用渠道)"""
    for arg in (build_args.split() if isinstance(build_args, str) else (build_args or [])):
        if arg.startswith("-DAVOX_DIST_FLAVOR="):
            return arg.split("=", 1)[1].strip()
    return "commercial"

def build_module(module_name, bOnlyMake=False,build_args="",bself=False):
    """
    构建一个模块，使用CMake进行配置和编译。
    """
    # 获取当前要生成的平台名
    target_system = get_current_target()
    # 获取构建路径
    build_dir = os.path.join(get_build_path(target_system),module_name)
    # 新增CMake缓存检测
    cmake_cache = os.path.join(build_dir, "CMakeCache.txt")
    # 渠道变更强制重配置: 已有cache会跳过configure, 新-flavor参数被忽略静默按旧渠道出包
    if not AVOX_FORCE_REBUILD and os.path.exists(cmake_cache):
        cached_flavor = _cached_dist_flavor(cmake_cache)
        requested_flavor = _requested_dist_flavor(build_args)
        if cached_flavor != requested_flavor:
            print(f"发行渠道变更: {cached_flavor} -> {requested_flavor}, 删除缓存强制重配置")
            try:
                os.remove(cmake_cache)
            except Exception as e:
                print(f"删除 CMake 缓存文件时发生错误: {e}")
    # 强制重建构建，删除 CMakeCache.txt 文件
    if AVOX_FORCE_REBUILD and os.path.exists(cmake_cache):
        print(f"强制删除 CMake 缓存文件: {cmake_cache}")
        try:
            os.remove(cmake_cache)
        except FileNotFoundError as e:
            print(f"删除 CMake 缓存文件时遇到文件不存在错误: {e}, 继续构建...")
        except Exception as e:
            print(f"删除 CMake 缓存文件时发生错误: {e}")
    # 确保构建目录存在
    os.makedirs(build_dir, exist_ok=True)  
    print(f"构建目录: {build_dir}")   
    # 切换到构建目录
    os.chdir(build_dir)           
    if AVOX_FORCE_REBUILD or not os.path.exists(cmake_cache):
        print(f"开始构建项目: {module_name}  目标平台: {target_system}  宿主平台: {get_host_system()}  BUILD_TYPE: {AVOX_BUILD_TYPE}") 
        os.makedirs(build_dir, exist_ok=True)         
        # 生成 CMake 参数
        cmake_args = [
            f"-DCMAKE_BUILD_TYPE={AVOX_BUILD_TYPE}"
        ]
        # CI 环境标识
        if os.environ.get('CMAKE_CI') == 'ON':
            cmake_args.append("-DCMAKE_CI=ON")
        if build_args:
            cmake_args += build_args.split() if isinstance(build_args, str) else build_args
        if target_system == "android":
            build_android(cmake_args)
        elif target_system == "windows":
            build_windows(cmake_args)
        elif target_system == "ios": 
            build_ios(cmake_args)
        elif target_system == "macos":
            build_macos(cmake_args)
        elif target_system == "linux":  
            build_linux(cmake_args)
        if bself:
            cmake_cmd = ["cmake","../../../"] + cmake_args
        else:
            cmake_cmd = ["cmake","../../../3rdparty/"+ module_name] + cmake_args
        cmake_cmd_str = " ".join(cmake_cmd)
        print(f"生成 CMake 项目:\n{cmake_cmd_str}")
        cmake_ret = os.system(cmake_cmd_str)
        if cmake_ret != 0:
            print(f"CMake 配置失败，错误码: {cmake_ret}")
            return False
    else:
        print(f"检测到已有CMake配置，跳过生成步骤")
        # 打印cache里实际生效的渠道, 避免误以为本次参数已注入
        print(f"生效发行渠道: {_cached_dist_flavor(cmake_cache)} (来自CMakeCache)")
    if bOnlyMake:
        return True

    def _extract_errors(output):
        """从构建输出中提取关键错误信息"""
        error_lines = []
        error_keywords = [
            "error LNK", "fatal error", "error C", "error D",
            "unresolved external", "无法解析的外部符号",
            "clang++: error", "ninja: build stopped",
            "error:", "FAILED:", "error generated",
        ]
        for line in output.splitlines():
            lower = line.lower()
            if any(kw.lower() in lower for kw in error_keywords):
                error_lines.append(line.strip())
        return error_lines

    build_proc = subprocess.Popen(
        ["cmake", "--build", ".", "--config", AVOX_BUILD_TYPE, "--parallel"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace"
    )
    build_output = ""
    for line in build_proc.stdout:
        print(line, end="")
        build_output += line
    build_proc.wait()

    failed = False
    if build_proc.returncode != 0:
        failed = True
    # 检查输出中是否有失败字样（Windows MSBuild 和 Android Ninja 共用）
    if "构建失败" in build_output or "FAILED:" in build_output or "ninja: build stopped" in build_output:
        failed = True
    # Windows MSBuild 特定错误
    if "error LNK" in build_output or "fatal error" in build_output:
        failed = True
    # Android Ninja 特定错误
    if "clang++: error" in build_output:
        failed = True

    if failed:
        errors = _extract_errors(build_output)
        print(f"\n{'='*60}")
        print(f"构建失败，错误码: {build_proc.returncode if build_proc.returncode != 0 else 1}")
        if errors:
            print("失败原因:")
            for e in errors[:10]:
                print(f"  {e}")
        print(f"{'='*60}\n")
        return False

    if bself:
        # SWIG 显式关闭时跳过 AvoxWrapper: CMake 侧 if(AVOX_ENABLE_SWIG) 不加载 swig 子目录,
        # AvoxWrapper target 不存在, 无条件 --target AvoxWrapper 会报 unknown target 误判构建失败
        if "AVOX_ENABLE_SWIG=OFF" in (build_args or ""):
            print("SWIG 已关闭 (AVOX_ENABLE_SWIG=OFF), 跳过 AvoxWrapper")
            print(f"项目构建完成: {module_name}")
            return True
        swig_proc = subprocess.Popen(
            ["cmake", "--build", ".", "--config", AVOX_BUILD_TYPE, "--target", "AvoxWrapper"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace"
        )
        swig_output = ""
        for line in swig_proc.stdout:
            print(line, end="")
            swig_output += line
        swig_proc.wait()
        swig_failed = False
        if swig_proc.returncode != 0:
            swig_failed = True
        if "构建失败" in swig_output or "FAILED:" in swig_output or "ninja: build stopped" in swig_output:
            swig_failed = True
        if "error LNK" in swig_output:
            swig_failed = True
        if swig_failed:
            errors = _extract_errors(swig_output)
            print(f"\n{'='*60}")
            print(f"SWIG AvoxWrapper 构建失败，错误码: {swig_proc.returncode if swig_proc.returncode != 0 else 1}")
            if errors:
                print("失败原因:")
                for e in errors[:10]:
                    print(f"  {e}")
            print(f"{'='*60}\n")
            return False
        else:
            print("SWIG AvoxWrapper 构建完成")
    print(f"项目构建完成: {module_name}")
    return True
 
def build_self(build_args="",bOnlyMake=False):
    if not build_module("avox",bOnlyMake,build_args,True):
        sys.exit(1)       
    
def build_android(cmake_args): 
    global AVOX_TARGET_ARCH,AVOX_NDK_ROOT    
    # 先检查预设路径是否存在
    if not os.path.exists(AVOX_NDK_ROOT):
        # 尝试从环境变量获取
        env_ndk = os.environ.get('ANDROID_NDK')
        if env_ndk and os.path.exists(env_ndk):
            AVOX_NDK_ROOT = os.path.normpath(env_ndk)
            print(f"使用环境变量 NDK_ROOT: {AVOX_NDK_ROOT}")
        else:
            raise ValueError(f"NDK 路径不存在: {AVOX_NDK_ROOT}\n请设置有效的 AVOX_NDK_ROOT 或 NDK_ROOT 环境变量")
    if AVOX_TARGET_ARCH not in AVOX_NDK_ARCH_LIST:
        print(f"警告：不支持的 Android 架构 {AVOX_TARGET_ARCH}，自动切换为 {AVOX_NDK_ARCH_LIST[0]}")
        AVOX_TARGET_ARCH = AVOX_NDK_ARCH_LIST[0]
        # 构造NDK自带的工具链路径
    ndk_toolchain = os.path.join(AVOX_NDK_ROOT, "build/cmake/android.toolchain.cmake")
    ndk_toolchain = ndk_toolchain.replace('\\', '/')
    # 使用正斜杠替换反斜杠
    print(f"NDK工具链路径: {ndk_toolchain}")
    print(f"NDK_ABI: {AVOX_TARGET_ARCH}")
    if not os.path.exists(ndk_toolchain):
        raise ValueError(f"在NDK目录中未找到工具链文件: {ndk_toolchain}")  
    # 平台差异处理逻辑（新增）
    if get_host_system() == "windows":
        # Windows系统需要显式指定ninja路径
        ninja_path = AVOX_NINJA_PATH.replace('\\', '/')
    else:
        # Linux/macOS默认使用系统路径中的ninja
        ninja_path = "ninja"  
    # 16KB 对齐支持; 模块自带 CMAKE_EXE_LINKER_FLAGS 时(如 sentencepiece 的 -llog)合并而非覆盖(同名 -D 后者生效)
    page_align = "-Wl,-z,max-page-size=16384"
    # Release 默认不带符号: -s 剥掉 .symtab 与调试段(保留 .dynsym 导出符号, 不影响运行), .so 体积大幅减小
    strip_flags = " -s" if AVOX_BUILD_TYPE == "Release" else ""
    exe_linker_flags = page_align
    for _arg in cmake_args:
        if isinstance(_arg, str) and _arg.startswith("-DCMAKE_EXE_LINKER_FLAGS="):
            _extra = _arg.split("=", 1)[1]
            if _extra and _extra != page_align:
                exe_linker_flags = f"{page_align} {_extra}"
    cmake_args += [
        f"-DCMAKE_SYSTEM_NAME=Android",
        f"-DCMAKE_ANDROID_STL_TYPE=c++_shared",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk_toolchain}",
        f"-DANDROID_NDK={AVOX_NDK_ROOT}",
        f"-DANDROID_ABI={AVOX_TARGET_ARCH}",
        f"-DCMAKE_ANDROID_ARCH_ABI={AVOX_TARGET_ARCH}",
        f"-DANDROID_PLATFORM={AVOX_NDK_PLATFORM}",
        f"-DCMAKE_MAKE_PROGRAM={ninja_path}",
        f"-DCMAKE_SHARED_LINKER_FLAGS={page_align}{strip_flags}",
        f"-DCMAKE_EXE_LINKER_FLAGS={exe_linker_flags}{strip_flags}",
        "-G", "Ninja"
    ]   

# 新增 iOS 构建函数
def build_ios(cmake_args):
    global AVOX_TARGET_ARCH
    if AVOX_TARGET_ARCH not in AVOX_IOS_ARCH_LIST:
        print(f"警告：不支持的 iOS 架构 {AVOX_TARGET_ARCH}，自动切换为 {AVOX_IOS_ARCH_LIST[0]}")
        AVOX_TARGET_ARCH = AVOX_IOS_ARCH_LIST[0]
    
    # 检查 Xcode 路径
    xcode_path = subprocess.check_output(['xcode-select', '-p']).decode('utf-8').strip()
    toolchain_path = os.path.join(xcode_path, 'Toolchains/XcodeDefault.xctoolchain')
    print(f"xcode工具链路径: {toolchain_path}")
    if not os.path.exists(toolchain_path):
        raise ValueError(f"未找到 Xcode 工具链: {toolchain_path}")

    # 获取项目根目录
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    # 拼接 iOS 工具链文件路径
    ios_toolchain_path = os.path.join(project_root, "cmake", "ios.toolchain.cmake")
    
    if not os.path.exists(ios_toolchain_path):
        raise ValueError(f"未找到 iOS 工具链文件: {ios_toolchain_path}")
   
    cmake_args += [ 
        # # 指定工具链文件路径   
        f"-DCMAKE_TOOLCHAIN_FILE={ios_toolchain_path}",  
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
        "-DCMAKE_C_COMPILER=/usr/bin/clang",        
        "-DCMAKE_SYSTEM_NAME=iOS",
        "-DCMAKE_OSX_SYSROOT=iphoneos",                           
        f"-DCMAKE_OSX_ARCHITECTURES={AVOX_TARGET_ARCH}",
        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={AVOX_IOS_DEPLOYMENT_TARGET}", 
        "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO",  
        # 非发布版本可关闭签名
        # "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=",
        "-DPLATFORM=OS",
        "-G", "Xcode"       
    ]
    print(f"ios cmake_args: {cmake_args}")

# macOS 构建函数(与 iOS 共用 ios.toolchain.cmake, 系统由 PLATFORM 区分, CMAKE_SYSTEM_NAME 自动为 Darwin)
def build_macos(cmake_args):
    global AVOX_TARGET_ARCH
    # arch -> 工具链 PLATFORM / CMAKE_OSX_ARCHITECTURES
    platform_map = {"arm64": "MAC_ARM64", "x64": "MAC", "x86_64": "MAC", "universal": "MAC_UNIVERSAL"}
    arch_map = {"arm64": "arm64", "x64": "x86_64", "x86_64": "x86_64", "universal": "arm64;x86_64"}
    if AVOX_TARGET_ARCH not in platform_map:
        print(f"警告：不支持的 macOS 架构 {AVOX_TARGET_ARCH}，自动切换为 arm64")
        AVOX_TARGET_ARCH = "arm64"
    # 检查 Xcode 路径
    xcode_path = subprocess.check_output(['xcode-select', '-p']).decode('utf-8').strip()
    toolchain_path = os.path.join(xcode_path, 'Toolchains/XcodeDefault.xctoolchain')
    print(f"xcode工具链路径: {toolchain_path}")
    if not os.path.exists(toolchain_path):
        raise ValueError(f"未找到 Xcode 工具链: {toolchain_path}")
    # 获取项目根目录
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    ios_toolchain_path = os.path.join(project_root, "cmake", "ios.toolchain.cmake")
    if not os.path.exists(ios_toolchain_path):
        raise ValueError(f"未找到 iOS 工具链文件: {ios_toolchain_path}")
    cmake_args += [
        f"-DCMAKE_TOOLCHAIN_FILE={ios_toolchain_path}",
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
        "-DCMAKE_C_COMPILER=/usr/bin/clang",
        f"-DCMAKE_OSX_ARCHITECTURES={arch_map[AVOX_TARGET_ARCH]}",
        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={AVOX_MACOS_DEPLOYMENT_TARGET}",
        "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO",
        f"-DPLATFORM={platform_map[AVOX_TARGET_ARCH]}",
        "-G", "Xcode"
    ]
    print(f"macos cmake_args: {cmake_args}")

def build_windows(cmake_args):
    global AVOX_TARGET_ARCH
    if AVOX_TARGET_ARCH not in AVOX_WIN_VS_ARCH_LIST:
        print(f"警告：不支持的 Windows 架构 {AVOX_TARGET_ARCH}，自动切换为 {AVOX_WIN_VS_ARCH_LIST[0]}")
        AVOX_TARGET_ARCH = AVOX_WIN_VS_ARCH_LIST[0]
    cmake_args += [
        "-A", AVOX_TARGET_ARCH,
        "-G", f"\"{AVOX_WIN_VS_VERSION}\"" 
    ]

def build_linux(cmake_args):
    print(f"liunx cmake_args: {cmake_args}")
    cmake_args += ["-G", "Unix Makefiles"]     

# 获取各系统生成库库后缀名
def get_system_lib_suffix():
    if get_current_target() == "windows":
        return ".lib"
    elif get_current_target() == "android":
        return ".so"
    elif get_current_target() == "linux":
        return ".so"
    else:
        return ".a"

# 检查模块是否已构建(产物在 build/<system>/<module> 下)
def check_module(module_name, dll_name):
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    suffix = get_system_lib_suffix()
    module_dir = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}", module_name) 
    current_target = get_current_target()
    build_type = AVOX_BUILD_TYPE    
    # 各系统查找路径表
    path_templates = {
        "windows": [
            os.path.join(module_dir, f"{build_type}/{dll_name}{suffix}"),
            os.path.join(module_dir, f"{build_type}/{dll_name}d{suffix}")  
        ],
        "linux": [
            os.path.join(module_dir, f"lib{dll_name}{suffix}"),
            os.path.join(module_dir, f"lib{dll_name}d{suffix}"),
            os.path.join(module_dir, f"lib{dll_name}.a"),
            os.path.join(module_dir, f"lib{dll_name}d.a"),
            f"/usr/lib/{dll_name}{suffix}",
            f"/usr/local/lib/{dll_name}{suffix}"
        ],
        "android": [os.path.join(module_dir, f"lib{dll_name}{suffix}"), 
                    os.path.join(module_dir, f"lib{dll_name}d{suffix}"),
                    os.path.join(module_dir, f"lib{dll_name}.a"),
                    os.path.join(module_dir, f"lib{dll_name}d.a")],
        "ios": [os.path.join(module_dir, f"{build_type}-iphoneos/lib{dll_name}{suffix}"),
                os.path.join(module_dir, f"{build_type}-iphoneos/lib{dll_name}d{suffix}")],
        "macos": [os.path.join(module_dir, f"{build_type}/lib{dll_name}{suffix}"),
                  os.path.join(module_dir, f"{build_type}/lib{dll_name}d{suffix}")]
    }    
    # 查找路径
    for path in path_templates.get(current_target, [os.path.join(module_dir, f"lib{dll_name}{suffix}")]):
        if os.path.exists(path):
            print(f"{module_name} path: {path}")
            return True    
    print(f"{module_name} not found")
    return False

def check_module_zlmediakit():
    # 获取项目根目录
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    suffix = get_system_lib_suffix()
    # 源目录路径,android与windows的路径不同
    if get_current_target() == "android":
        bin_dri = os.path.join(project_root, f"3rdparty/ZLMediaKit/release/{AVOX_TARGET_SYSTEM}/{AVOX_BUILD_TYPE}/libmk_api{suffix}")
    elif get_current_target() == "windows":
        bin_dri = os.path.join(project_root, f"3rdparty/zlmediakit/release/{AVOX_TARGET_SYSTEM}/{AVOX_BUILD_TYPE}/{AVOX_BUILD_TYPE}/mk_api{suffix}")
    elif get_current_target() == "ios":
        bin_dri = os.path.join(project_root, f"3rdparty/zlmediakit/release/{AVOX_TARGET_SYSTEM}/{AVOX_BUILD_TYPE}/{AVOX_BUILD_TYPE}/libmk_api{suffix}")
    elif get_current_target() == "macos":
        bin_dri = os.path.join(project_root, f"3rdparty/zlmediakit/release/{AVOX_TARGET_SYSTEM}/{AVOX_BUILD_TYPE}/{AVOX_BUILD_TYPE}/libmk_api{suffix}")
    elif get_current_target() == "linux":
        # linux大小写敏感
        bin_dri = os.path.join(project_root, f"3rdparty/ZLMediaKit/release/{AVOX_TARGET_SYSTEM}/{AVOX_BUILD_TYPE}/libmk_api{suffix}")
    # 打印bin_dri路径
    print(f"zlmediakit路径: {bin_dri}")
    # 检查bin_dri是否存在
    return os.path.exists(bin_dri)

def check_module_sherpa():
    # sherpa-onnx 的库文件路径
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    current_target = get_current_target()
    build_type = AVOX_BUILD_TYPE

    if current_target == "windows":
        # Windows 下 sherpa-onnx 生成 sherpa-onnx-c-api.lib
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sherpa-onnx/lib/{build_type}/sherpa-onnx-c-api.lib")
    elif current_target == "linux":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sherpa-onnx/lib/libsherpa-onnx-c-api.so")
    elif current_target == "android":
        # Android 使用静态库
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sherpa-onnx/lib/libsherpa-onnx-c-api.a")
    elif current_target == "ios":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sherpa-onnx/{build_type}-iphoneos/libsherpa-onnx-c-api.a")
    elif current_target == "macos":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sherpa-onnx/{build_type}/libsherpa-onnx-c-api.a")
    else:
        return False

    print(f"sherpa-onnx路径: {lib_path}")
    return os.path.exists(lib_path)

def check_module_sentencepiece():
    # sentencepiece 的库文件路径
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    current_target = get_current_target()
    build_type = AVOX_BUILD_TYPE

    if current_target == "windows":
        # Windows 下 sentencepiece 生成 sentencepiece.lib
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sentencepiece/src/{build_type}/sentencepiece.lib")
    elif current_target == "linux":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sentencepiece/src/libsentencepiece.so")
    elif current_target == "android":
        # Android 使用静态库
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sentencepiece/src/libsentencepiece.a")
    elif current_target == "ios":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sentencepiece/src/{build_type}-iphoneos/libsentencepiece.a")
    elif current_target == "macos":
        lib_path = os.path.join(project_root, f"build/{AVOX_TARGET_SYSTEM}/sentencepiece/src/{build_type}/libsentencepiece.a")
    else:
        return False

    print(f"sentencepiece路径: {lib_path}")
    return os.path.exists(lib_path)
    
def copy_glsl_files():
    # 获取当前脚本所在目录
    script_dir = os.path.dirname(__file__)
    # 源目录路径
    src_dir = os.path.join(script_dir, 'glsl/target')    
    # 目标目录路径
    dest_dir = os.path.join(
        script_dir,
        f"build/windows/avox/install/AMD64/{AVOX_BUILD_TYPE}/glsl"
    )    
    if not os.path.exists(src_dir):
        print(f"⚠️ GLSL源目录不存在: {src_dir}")
        return    
    try:
        # 创建目标目录（如果不存在）
        os.makedirs(dest_dir, exist_ok=True)
        # 复制所有文件（保留元数据）
        shutil.copytree(src_dir, dest_dir, dirs_exist_ok=True, copy_function=shutil.copy2)
        print(f"✅ 成功复制GLSL文件到: {dest_dir}")
    except Exception as e:
        print(f"❌ 文件复制失败: {str(e)}")

# ========== ONNX Runtime 查找函数 ==========

def get_onnxruntime_paths(platform, arch):
    """
    获取 ONNX Runtime 搜索路径列表
    优先级: 1. AVOX_EXTERNAL_LIBRARY_DIR 环境变量 2. 本地 3rdparty 3. 父目录 avc_library
    """
    paths = []

    # 1. 优先使用 AVOX_EXTERNAL_LIBRARY_DIR 环境变量
    external_lib = os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
    if external_lib:
        paths.append(os.path.join(external_lib, "3rdparty", "library", platform, "onnxruntime"))

    # 2. 本地 3rdparty 目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    paths.append(os.path.join(script_dir, "3rdparty", "library", platform, "onnxruntime"))

    # 3. 父目录的 avc_library
    paths.append(os.path.join(script_dir, "..", "avc_library", "3rdparty", "library", platform, "onnxruntime"))

    return paths

def find_onnxruntime(platform, arch, version="1.23.2"):
    """
    查找 ONNX Runtime 库目录并设置环境变量

    Args:
        platform: "windows", "android", "ios", "linux"
        arch: 架构名称，如 "x64", "arm64-v8a", "armeabi-v7a"
        version: ONNX Runtime 版本号

    Returns:
        找到的目录路径，未找到返回 None
    """
    paths = get_onnxruntime_paths(platform, arch)

    # 根据平台确定目录名称
    if platform == "windows":
        dir_name = f"onnxruntime-win-{arch}-{version}"
    elif platform == "android":
        # Android 优先使用静态库
        dir_name = f"onnxruntime-android-{arch}-static_lib-{version}"
    elif platform == "ios":
        dir_name = f"onnxruntime-ios-{arch}-{version}"
    elif platform == "linux":
        dir_name = f"onnxruntime-linux-{arch}-{version}"
    else:
        print(f"警告: 不支持的平台 {platform}")
        return None

    # 搜索目录
    for base_path in paths:
        check_dir = os.path.join(base_path, dir_name)
        if os.path.exists(check_dir):
            print(f"找到 onnxruntime 库目录: {check_dir}")

            # 设置环境变量供 sherpa-onnx 使用
            lib_dir = os.path.join(check_dir, "lib")
            include_dir = os.path.join(check_dir, "include")

            if os.path.exists(lib_dir) and os.path.exists(include_dir):
                os.environ["SHERPA_ONNXRUNTIME_LIB_DIR"] = lib_dir
                os.environ["SHERPA_ONNXRUNTIME_INCLUDE_DIR"] = include_dir
                print(f"设置 onnxruntime 环境变量:")
                print(f"  SHERPA_ONNXRUNTIME_LIB_DIR={lib_dir}")
                print(f"  SHERPA_ONNXRUNTIME_INCLUDE_DIR={include_dir}")
                return check_dir
            else:
                print(f"警告: 目录存在但缺少 lib/include 子目录: {check_dir}")

    print(f"警告: 未找到 onnxruntime 库目录 (平台: {platform}, 架构: {arch}, 版本: {version})")
    print(f"搜索路径: {paths}")

    if platform == "android":
        print(f"请先运行: python script/onnx/down_onnxruntime_android.py")
    elif platform == "windows":
        print(f"请先运行: python script/onnx/down_onnxruntime_windows.py")

    return None


def find_openssl(platform):
    """
    查找 OpenSSL 库目录 (include/ + lib/)

    搜索路径优先级:
      1. AVOX_EXTERNAL_LIBRARY_DIR 环境变量
      2. 本地 3rdparty/library/{platform}/openssl
      3. 父目录 avc_library/3rdparty/library/{platform}/openssl

    Returns:
      找到的目录路径，未找到返回 None
    """
    paths = []
    external_lib = os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
    if external_lib:
        paths.append(os.path.join(external_lib, "3rdparty", "library", platform, "openssl"))
    script_dir = os.path.dirname(os.path.abspath(__file__))
    paths.append(os.path.join(script_dir, "3rdparty", "library", platform, "openssl"))
    paths.append(os.path.join(script_dir, "..", "avc_library", "3rdparty", "library", platform, "openssl"))

    for base_path in paths:
        include_dir = os.path.join(base_path, "include", "openssl", "ssl.h")
        if not os.path.exists(include_dir):
            continue
        # lib 可能在 lib/ 或 {arch}/ 子目录下
        lib_dir = os.path.join(base_path, "lib")
        if os.path.exists(lib_dir):
            print(f"找到 OpenSSL 库目录: {base_path}")
            return base_path
        # avc_library 结构: openssl/arm64-v8a/libssl.so
        if platform == "android":
            for arch in ["arm64-v8a", "armeabi-v7a", "x86", "x86_64"]:
                arch_dir = os.path.join(base_path, arch)
                if os.path.exists(arch_dir):
                    print(f"找到 OpenSSL 库目录: {base_path} (arch: {arch})")
                    return base_path

    print(f"警告: 未找到 OpenSSL 库目录 (平台: {platform})")
    print(f"搜索路径: {paths}")
    return None
