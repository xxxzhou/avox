{
  "targets": [{
    "target_name": "avox_js",
    "sources": [
      "files/commonJAVASCRIPT_wrap.cxx",
      "nativeOb/JsMediaPlayerOb.cpp",
      "nativeOb/JsRtcEventOb.cpp",
      "nativeOb/JsLogOb.cpp",
      "nativeOb/JsSurfaceRenderOb.cpp",
      "nativeOb/JsRecorderOb.cpp",
      "nativeOb/JsEglHelper.cpp",
      "nativeOb/JsSessionObserverOb.cpp",
      "nativeOb/JsApprovalUiOb.cpp",
    ],
    "libraries": [
      "-lavox"
    ],    
    "include_dirs": [  
      "<!@(node -p \"require('node-addon-api').include\")", 
      "../../src",
      "../../swig",
      "../../3rdparty/khronos",
    ],
    "defines": [
      "NAPI_VERSION=8"
    ],
    "variables": {
      "config": "",
      "platform": ""
    },
    "conditions": [
      ['OS == "win"', { "platform": "AMD64" }],
      ['OS == "linux"', { "platform": "linux" }]
    ],
    "product_dir": "../../../build/windows/avox/install/AMD64/$(configuration)/",
    "configurations": {
      "Debug": {
        "defines": ["DEBUG"],
        "cflags": ["-g"],
        "library_dirs": ["../../build/windows/avox/install/AMD64/Debug"],
        # node-gyp 10.2在VS2022上默认选ClangCL工具集, 机器未装该组件, 钉回v143
        "msbuild_toolset": "v143",
        "msvs_settings": {
          "VCCLCompilerTool": {
            "ExceptionHandling": 1
          }
        }
      },
      "Release": {
        "defines": ["NDEBUG"],
        "cflags": ["-O2"],
        "library_dirs": ["../../build/windows/avox/install/AMD64/Release"],
        "msbuild_toolset": "v143",
        "msvs_settings": {
          "VCCLCompilerTool": {
            "WholeProgramOptimization": "false",
            "AdditionalOptions": ["/bigobj"],
            "ExceptionHandling": 1
          }
        }
      }
    },
  }]
}