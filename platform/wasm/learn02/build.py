import os

MAKE_WASM_PROJECT = "emcmake cmake .."
# BUILD_WASM_PROJECT = "emmake make -s WASM=1"
BUILD_WASM_PROJECT = "emmake make -s EXPORTED_FUNCTIONS=_int_sqrt -s EXTRA_EXPORTED_RUNTIME_METHODS=[ccall]"
# BUILD_WASM_JS = "emcc libfunction2.a -o function.html -s EXPORTED_FUNCTIONS=_int_sqrt -s EXPORTED_RUNTIME_METHODS=ccall"
BUILD_WASM_JS = "emcc libfunction2.a -o function.html -s EXPORTED_FUNCTIONS=_int_sqrt -s EXPORTED_RUNTIME_METHODS=ccall,cwrap"
if __name__ == "__main__":
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))      
    # 创建一个用于存放构建文件的目录，例如 "build_wasm"
    build_dir = os.path.join(project_root, "build")
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
    os.chdir(build_dir)  
    # 调用Emscripten的命令行工具进行构建项目
    os.system(MAKE_WASM_PROJECT)
    # 调用Emscripten的命令行工具进行编译项目
    os.system(BUILD_WASM_PROJECT)
    #  转成JS
    # os.system(BUILD_WASM_JS)