import os
import subprocess

# 用来记录流程，实际不可用，类似伪代码

# 假设 MSYS2 安装在 C:\msys64 目录下，根据实际情况修改
MSYS2_INSTALL_DIR = "C:\\msys64"

# 构建 pacman.exe 的完整路径
pacman_path = os.path.join(MSYS2_INSTALL_DIR, "usr", "bin", "pacman.exe")
# 构建 bash.exe 的完整路径
bash_path = os.path.join(MSYS2_INSTALL_DIR, "usr", "bin", "bash.exe")
# 构建 mingw32-make.exe 的完整路径
make_path = os.path.join(MSYS2_INSTALL_DIR, "mingw64", "bin", "mingw32-make.exe")
# 构建 mingw64.exe 的完整路径
mingw64_path = os.path.join(MSYS2_INSTALL_DIR, "mingw64.exe")


def is_software_repository_up_to_date():
    """
    检查软件库是否为最新
    """
    check_command = f"{pacman_path} -Qu"
    # 执行检查命令，若返回值为 0 则表示有可更新的软件包，否则表示软件库是最新的
    return os.system(check_command)!= 0

def update_software_repository():
    """
    更新软件库数据（如果不是最新的）
    """
    if not is_software_repository_up_to_date():
        print("软件库已是最新，无需更新。")
        return
    command = f"{pacman_path} -Sy"
    # 执行命令并记录输出和错误信息
    execute_command(command)

def is_package_installed(package_name):
    """
    检查指定的软件包是否已安装
    """
    check_command = f"{pacman_path} -Qs {package_name}"
    return os.system(check_command) == 0

def install_required_packages():
    """
    安装开发工具包、yasm 或 nasm、pkg-config 和 zlib（如果未安装）
    """
    # 安装开发工具包（如果未安装）
    if not is_package_installed("base-devel"):
        install_command = f"{pacman_path} -S base-devel"
        execute_command(install_command)
    else:
        print("base-devel 已安装，无需安装。")

    # 检查并安装 yasm 或 nasm（如果未安装）
    if not is_package_installed("yasm"):
        install_command = f"{pacman_path} -S yasm"
        execute_command(install_command)
    else:
        print("yasm 已安装，无需安装。")

    if not is_package_installed("nasm"):
        install_command = f"{pacman_path} -S nasm"
        execute_command(install_command)
    else:
        print("nasm 已安装，无需安装。")

    # 检查并安装 pkg-config 和 zlib（如果未安装）
    if not is_package_installed("pkg-config"):
        install_command = f"{pacman_path} -S pkg-config"
        execute_command(install_command)
    else:
        print("pkg-config 已安装，无需安装。")

    if not is_package_installed("zlib"):
        install_command = f"{pacman_path} -S zlib"
        execute_command(install_command)
    else:
        print("zlib 已安装，无需安装。")

def execute_command(command):
    """
    在当前环境中执行命令并记录输出和错误信息到日志文件
    """
    try:
        # 执行命令
        result = os.system(command)
        # 根据命令执行结果判断是否成功
        if result!= 0:
            print(f"Command '{command}' failed with exit code {result}.")
            # 记录错误信息到日志文件（如果需要）
            # with open(log_file, "a") as log:
            #     log.write(f"Command '{command}' failed with exit code {result}.\n")
            sys.exit(1)
    except Exception as e:
        print(f"Error running command '{command}': {e}")
        # 记录异常信息到日志文件（如果需要）
        # with open(log_file, "a") as log:
        #     log.write(f"Error running command '{command}': {e}\n")
        sys.exit(1)

def configure_ffmpeg():
    """
    配置 FFmpeg
    """
    ffmpeg_build_dir = "../../build/windows/ffmpeg"
    ffmpeg_configure_options = [
        "--disable-static",
        "--enable-shared",
        "--enable-version3",
        "--disable-ffplay",
        "--enable-ffmpeg",
        "--disable-x86asm"
    ]
    # 构建完整的 configure 命令
    configure_command = [bash_path, "./configure", f"--prefix={ffmpeg_build_dir}"] + ffmpeg_configure_options
    try:
        # 使用 subprocess.run 执行命令
        result = subprocess.run(configure_command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        # 打印输出和错误信息（如果需要）
        print(result.stdout)
        print(result.stderr)
    except subprocess.CalledProcessError as cpe:
        print(f"Command execution failed with exit code {cpe.returncode}")
        print(cpe.stderr)

def compile_ffmpeg():
    """
    编译 FFmpeg
    """
    # 构建完整的 make 命令
    compile_command = f"{make_path} -j4"
    # 执行命令并记录输出和错误信息
    execute_command(compile_command)

def install_ffmpeg():
    """
    安装 FFmpeg
    """
    # 构建完整的 make install 命令
    install_command = f"{make_path} install -j4"
    # 执行命令并记录输出和错误信息
    execute_command(install_command)

def main():
    # 更新软件库数据（如果不是最新的）
    update_software_repository()
    # 安装所需的软件包（如果未安装）
    install_required_packages()
    # 配置 FFmpeg
    # configure_ffmpeg()
    # 编译 FFmpeg
    compile_ffmpeg()
    # 安装 FFmpeg
    install_ffmpeg()

if __name__ == "__main__":
    main()