# WebRTC 拉流连接测试 (tools/src/webrtc)

输入 ZLM 播放地址建立 HTTP 信令 (C++ 侧 `TestSdpOb` 同款交互: 本地 offer 自动 POST,
远端 answer 回填), 以 **first_video_frame** 判定链路是否正常。等价于无头版
[platform/godot/tools/tests/test_rtc.gd](../../tests/test_rtc.gd) 的带界面形态。

## 运行

```powershell
# hub 卡片入口「WebRTC 测试」, 或直接开场景
godot --path platform/godot/tools res://src/webrtc/main.tscn
```

前提: `addons/avox_godot` 已部署, 且 `bin/plugins/` 里有 `avox_webrtc` 动态插件。

## 地址格式 (输入框两种都收)

| 格式 | 处理 |
|------|------|
| `http(s)://host/index/api/webrtc?app=live&stream=test&type=play` | ZLM 信令原链, 直连 |
| `webrtc(s)://host[:port]/app/stream` | ZLM 播放页链接, 自动转上行信令原链 (443 → https) |

最近 10 条地址持久化于 `user://webrtc_test.cfg`, 下拉可重连。

## 两种信令模式 (勾选「自定义信令」切换)

默认走内置信令: `connect_signaling` → C++ `createZlTestSdpAgent` ([TestSdpOb](../../../../src/avox_zlmediakit/TestSdpOb.cpp)) 自动交换。

勾选「自定义信令」后不再挂内置 agent, 由 GDScript 参考实现完成同样的交换
(`main.gd` 的 `_on_local_sdp` / `_on_sdp_http_done`, **即 TestSdpOb 的 GDScript 镜像,
抄走改造即成自有信令**):

```
open_rtc()                        # 不挂 agent, open 后播放器本地生成 offer
  └─ local_sdp 信号 (offer 明文) ──→ HTTP POST 到信令服务器 (body = SDP 明文,
                                     Content-Type: text/plain;charset=utf-8)
                                      └─ 响应 JSON {code, msg, sdp}
                                           └─ code==0 → set_remote_sdp(sdp) 回填 answer
```

改造要点:

- **非 trickle 服务器** (ZLM): answer 里自带 ICE 候选, 到上一步就够, 不用管 ICE。
- **trickle 服务器**: 再监听 `ice_candidate` 信号把候选送出去, 收到对端候选用
  `add_ice_candidate(candidate, mid, mline)` 回填。
- **推流方向**: `set_roll_type(1)` (ANSWER 被动) + ZLM `type=push`, 收到对端 offer
  同样用 `set_remote_sdp` 回填。
- 换 WebSocket/私有协议只改「POST + 解析响应」那段, 其余流程不变。

## 判定与状态

- **连接**: 信令交换 → ICE → connected → 出图; 状态栏实时显示 conn 状态与
  `fps / loss / rtt / 分辨率` (0.5s 节流)。
- **判定行**: 首帧打 `PASS`, `rtc_error` 打 `FAIL`
  (`[AVOX][TEST] case=rtc-ui …`, 与 test_rtc.gd 同款, 供 collect_verdicts.py 汇总)。
- 断开 / 重连按钮分别走 `close_rtc` / `reconnect_rtc`; Esc 或「返回主页」回 hub。
