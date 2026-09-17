"""干净构建启动器: 剥掉宿主注入的代理环境变量(MSBuild ZERO_CHECK 的
ProcessStartInfo.EnvironmentVariables 是大小写不敏感字典, HTTP_PROXY/http_proxy
同时存在会抛 MSB6001 "已添加项"), 然后直接调 cmake 配置 + 构建。

用法: python clean_build.py <config> [cmake 额外参数...]
"""
import os
import subprocess
import sys

# 1) 清洗环境: 删除 MSBuild 会炸的代理变量
for k in list(os.environ.keys()):
    if k.lower() in ("http_proxy", "https_proxy", "all_proxy", "no_proxy"):
        del os.environ[k]
# 同时剔除可能被宿主动态注入的大小写重复
os.environ.pop("HTTP_PROXY", None)
os.environ.pop("http_proxy", None)
os.environ.pop("HTTPS_PROXY", None)
os.environ.pop("https_proxy", None)

BUILD_DIR = r"D:\Work\github\avox\build\windows\avox"
CONFIG = sys.argv[1] if len(sys.argv) > 1 else "RelWithDebInfo"
EXTRA = sys.argv[2:]

# 2) 配置: -G 的生成器名作为一个完整 argv 元素传入, 不经 shell 拼接, 避免
#    build_common.py 的 os.system 拼串把 "Visual Studio 17 2022" 二次加引号
configure = [
    "cmake", r"D:\Work\github\avox",
    f"-DCMAKE_BUILD_TYPE={CONFIG}",
    "-DAVOX_DIST_FLAVOR=commercial",
    "-A", "x64",
    "-G", "Visual Studio 17 2022",
] + EXTRA

print("=== configure ===", flush=True)
print(" ".join(configure), flush=True)
r = subprocess.run(configure, cwd=BUILD_DIR)
if r.returncode != 0:
    print(f"configure FAILED rc={r.returncode}", flush=True)
    sys.exit(1)

# 3) 构建
build = ["cmake", "--build", ".", "--config", CONFIG, "--parallel"]
print("=== build ===", flush=True)
print(" ".join(build), flush=True)
r = subprocess.run(build, cwd=BUILD_DIR)
print(f"build rc={r.returncode}", flush=True)
sys.exit(r.returncode)
