# -*- coding: utf-8 -*-
# AvoxPlayer 插件编辑器便捷测试: 在 UE 编辑器 Output Log 的 Cmd 输入框执行:
#   py <avox仓库>/platform/ue/plugin/demo/AvoxDemoSetup.py
# 会生成一个带 AvoxMediaPlayer 组件的 Actor 并打开播放, 视频纹理输出到组件 VideoTexture。
# 显示: 建一个材质 (TextureSampleParameter2D, 参数名如 AvoxTex), 用 MID SetTextureParameterValue
# 绑定 VideoTexture; 或 UMG Image 直接设 Brush 资源为 VideoTexture。
import unreal

# ← 改成你的播放地址 (rtmp/rtsp/http/本地路径)
URL = "rtmp://localhost/live/test"

subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = subsystem.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 100))
if not actor:
    unreal.log_error("[AvoxDemo] 生成 Actor 失败")
else:
    actor.set_actor_label("AvoxPlayerDemo")
    comp = actor.add_component_by_class(unreal.AvoxMediaPlayerComponent, False, unreal.Transform())
    comp.set_editor_property("url", URL)
    comp.set_editor_property("hard_decode", True)
    comp.open(URL)
    unreal.log("[AvoxDemo] 已生成 AvoxPlayerDemo Actor 并 open: %s" % URL)
    unreal.log("[AvoxDemo] 播放后组件 VideoTexture 即视频纹理, 绑到材质参数或 UMG Image 显示; GetState/OnStateChanged 可监听状态")
