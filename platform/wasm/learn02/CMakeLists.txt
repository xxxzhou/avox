
# set(EXPORTED_FUNCTIONS "_int_sqrt")
# # -s WASM=1 — 指定我们想要的 wasm 输出形式,否则只会输出asm.js
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1")    
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_RUNTIME_METHODS=ccall,cwrap")     
# # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --shell-file ${CMAKE_SOURCE_DIR}/samples/wasmtest/test.html")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS}") 
# set(CMAKE_CXX_COMPILER emcc)
# add_library(function2 ../learn01/hello_function.cpp)
add_library(function2 hello_function.cpp)
# https://github.com/OlivierLDff/QtWasmCMake/blob/master/QtWasmCMake.cmake
target_link_libraries(function2 PUBLIC "-s USE_WEBGL2=1")
# https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html
# C++的Bind
target_link_libraries(function2 PUBLIC "--bind")

set_target_properties(
    function2 
    PROPERTIES 
    LINK_FLAGS "--bind -s EXPORTED_FUNCTIONS=[int_sqrt] -s EXTRA_EXPORTED_RUNTIME_METHODS=[ccall]")