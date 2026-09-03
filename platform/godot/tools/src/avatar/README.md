# Avatar 工具目录 (src/avatar)

视频驱动 Avatar 工具(视频→ARKit52 驱动 avatar)的**场景 + 模型/材质**统一放这里。
**材质按部位分类**:脸部 → `face/`,身体 → `body/`,手部 → `hand/`。

## 目录结构

| 路径 | 用途 |
|---|---|
| `main.tscn` + `main.gd` | **工具场景**(hub 卡片「视频驱动 Avatar」入口;左视频/右 3D,`VideoFaceNode` 驱动) |
| `avatar.gltf` + `avatar.bin` | **主模型**(mesh+骨骼),引用 `face/`/`body/` 的贴图;工具从这里加载 |
| `face/` | 脸部材质:眼睛/皮肤/牙齿 baseColor(驱动 ARKit52 的 mesh 所在) |
| `body/` | 身体材质:躯干/衣服(bottom·top·footwear)/头发/眼镜的 baseColor·normal·metallicRoughness |
| `hand/` | 手部材质(预留;当前示例模型无独立手 mesh,仅 `.gitkeep`) |
| `README.md` | 本说明 |

> `.gltf` 由原始 `avatar.glb` 拆解而来(脚本 `tmp/extract_avatar_glb.py`):GLB 内嵌贴图按 mesh
> 归类提取成 `face/`、`body/` 下可独立编辑的 PNG,`.gltf` 引用它们,`.bin` 存顶点/骨骼/blendshape 数据。
> 想换回单体模型,直接用带 ARKit52 名的 `.glb` 覆盖/换加载路径即可。

## 当前主模型

Ready Player Me 的 `brunette.glb`(TalkingHead 仓库样本):
- **51/52 ARKit blendshape** 全对上(仅缺 `_neutral` 中性占位, 工具已 skip)。
- blendshape 位于 `Wolf3D_Head` / `EyeLeft` / `EyeRight` / `Wolf3D_Teeth` mesh(即 `face/` 贴图对应部件);
  另有 viseme/eyesClosed/mouthOpen 等非 ARKit 名, 工具忽略。
- 身体 = `Wolf3D_Body` / `Outfit_*` / `Hair` / `Glasses`(`body/` 贴图对应部件)。

## 加载路径

工具的 `DEFAULT_AVATAR_PATHS` 依次尝试:
`res://src/avatar/avatar.gltf` → `res://src/avatar/avatar.glb` → `user://avatar.glb` → `res://avatar.glb` → `res://assets/avatar.glb`

## 换你自己的模型 / 改材质

- **换模型**:需要**带 ARKit52 blendshape 名**(`jawOpen`/`eyeBlinkLeft`/…)的 `.glb`/`.gltf`。
  **VRM 不直接带 ARKit 名**(其预设存 VRM 扩展里), 需先转成 GLB+ARKit(Blender/转换工具)。
  可靠源:Ready Player Me(`models.readyplayer.me/{id}.glb?morphTargets=ARKit`)、TalkingHead 仓库 `avatars/*.glb`。
- **改材质**:直接替换 `face/`/`body/` 里对应的 PNG(保持文件名),Godot 重导入即生效。

## 测试视频 (驱动用的素材)

`fetch_test_clips.py`(本目录, 纯标准库, 搬机器不用 `pip install`)下载可驱动 avatar 的测试视频到 `test_clips/`:

```bash
python fetch_test_clips.py            # 下全部 → ./test_clips
python fetch_test_clips.py --list     # 只看目录
python fetch_test_clips.py --only headpose          # 按 tag 选
python fetch_test_clips.py --manual   # 说话/表情类的手动获取指引
```

- **自动下载** = Intel IoT 正脸/头部姿态/多人片段(已验证直链), 适合测 landmarker 头部跟随/多脸鲁棒。
- **说话/口型/大表情类无稳定直链**, 跑 `--manual` 看 Pixabay/Pexels/`yt-dlp` 手动指引, 下完丢进 `test_clips/` 即可。
- 管线内部缩 192×192, 挑片 480p 够: 正面顺光、脸占画面 ≥1/4、嘴部清晰、5–30s。
