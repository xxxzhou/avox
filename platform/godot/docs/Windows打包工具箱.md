# Windows 工具箱打包 (avox_tools.exe)

## 一键导出

```bash
cd platform/godot/tools
"D:/Work/godot/godot.exe" --headless --export-release "Windows Desktop" "../avox_tools_win/avox_tools.exe"
```

预设 `Windows Desktop` 在 `export_presets.cfg` (embed_pck, 资源全部内嵌 exe)。
注意: 需先创建导出目录 `platform/godot/avox_tools_win/` (Godot 不会自建)。

## 伴随依赖 (exe 同目录, 必须手动拷贝)

GDExtension 库走 PCK: `tools/gdext/avox_godot.dll` (真实文件, 非 junction) 会被打包,
Godot 4.2+ 运行时自动解包加载。**bin 是 junction, 导出器不跟随** —— 这就是
`avox_godot.gdextension` 的 release 条目指向 `res://gdext/` 的原因; deploy_godot.ps1
重新部署 bin 后需同步 `cp bin/avox_godot.dll gdext/`。

其余 avox 运行时库不进 PCK, 从 `tools/addons/avox_godot/bin/` 拷到 exe 旁:

```bash
cd platform/godot
cp tools/addons/avox_godot/bin/{avox.dll,AvoxWrapper.dll,avcodec-61.dll,avformat-61.dll,avutil-59.dll,swresample-5.dll,libssl-3-x64.dll,libcrypto-3-x64.dll,mk_api.dll,fdk-aac.dll,onnxruntime.dll,opencv_world4130.dll,sherpa-onnx-c-api.dll,sherpa-onnx-cxx-api.dll} avox_tools_win/
cp C:/Windows/System32/D3DCOMPILER_47.dll avox_tools_win/   # 系统 DLL, 缺失时补
cp -r tools/addons/avox_godot/bin/assets avox_tools_win/     # AssetManager 部署根 (manifest/glsl/config)
rm -rf avox_tools_win/assets/models avox_tools_win/assets/agent   # 可选大模型, 资源面板按需下载
```

排查 dll 依赖用 PE 导入表 (缺什么拷什么):

```python
import struct  # 解析 [libraries] 段附近勿放中文注释行, VariantParser 不识别 # 注释
```

## 坑

- `avox_godot.gdextension` 的 `[libraries]` 段内**不能放 `#` 注释行** (解析失败 →
  "No GDExtension library found"), 注释放段外用 `;`。
- dev/editor 走 debug 条目 (res:// bin junction); release 条目指向 res://gdext/。
  run_tools.py 启动 dev 时 CWD=Release 根, 与 tools.lnk 一致。
- 语音输入等子 Window 默认嵌入主窗口 (embed_subwindows), 坐标系是宿主画布逻辑像素。
