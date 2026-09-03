import subprocess
import os
import time
import webbrowser

MAKE_WASM_PROJECT = "emcc hello_function.cpp  -o function.html -s USE_PTHREADS=1 -s EXPORTED_RUNTIME_METHODS=ccall"

if __name__ == "__main__":
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__)))
    # 切换到构建目录
    os.chdir(project_root)  
    os.system(MAKE_WASM_PROJECT)
    # 启动新的http.server
    webbrowser.open("http://localhost:9980/function.html")
    
# http://localhost:9980/platform/wasm/learn01/function.html    
# http://localhost:9980/platform/wasm/learn01/index2.html    