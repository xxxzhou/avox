# UE4插件与纹理对接

> 整理自 aocec 仓库 `doc/ue4/UE4插件.md`、`UE4对接.md`, 2026-09 同步合并。

## UE4插件总体设计

分多模块, 主模块加载底层 aoce 并封装成 UE4 相应接口, 主模块提供一些所有分模块都需要的功能, 包含快速展示。

主要根据现在需求分如下分模块:

- **OpenCV**: 包含定位后查看偏移。
- **Rivermax**: 使用 Rivermax 发送/接受 UE4 纹理。

构建系统概念:

- **UHT(UnrealHeaderTool)**: 根据定义 UField/UFunction 等生成元数据, 供反射与蓝图等功能使用。
- **UBT(Unreal Build Tool)**: 使用 C# 来完成 CMake 工程编译, 各个模块间的 `*.Build.cs` 相当于 CMakeLists.txt 文件, 因此文件夹下有 `*.Build.cs` 文件的, 此文件夹就是一个模块。

在 win 平台下, dll 文件需要复制到生成的执行目录下, 可以使用 `PublicDelayLoadDLLs` 表明延迟加载这些 dll, 但是需要在对应 Plugins.cpp 中的 StartupModule 使用 `FPlatformProcess::GetDllHandle(path)` 加载指定路径下的 dll。

> avox 的 UE 插件(platform/ue, AvoxPlayer)沿用同款思路: Build.cs 链接 avox.lib 延迟加载 avox.dll 并 RuntimeDependencies 打包运行时 dll。

## 纹理/引擎对接注意点

- [android下vulkan与opengles纹理互通](https://zhuanlan.zhihu.com/p/302285687)
- [Vulkan与DX11交互](https://zhuanlan.zhihu.com/p/349534525)
- vulkan 与 opengles 纹理交互需要 android api >= 26 才行。
- DX12 相关: [Dx12与CUDA不同线程纹理交互](https://github.com/xxxzhou/aocec/blob/main/doc/thirdparty/Dx12%E4%B8%8ECUDA%E4%B8%8D%E5%90%8C%E7%BA%BF%E7%A8%8B%E7%BA%B9%E7%90%86%E4%BA%A4%E4%BA%92.md)(aocec 仓库 doc/thirdparty/ 下)。
