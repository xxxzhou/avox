#!/bin/bash
# Linux/WSL 全量播放回归一键脚本: 本地起 MediaServer(高端口) + 推流 + 全量矩阵
# 用法: bash script/testenv/run_linux_full.sh [--skip=额外跳过]
# 依赖: build/linux/avox/install/x86_64/{playtest} 已构建, PATH 有 ffmpeg
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ZLM_DIR="$ROOT/3rdparty/ZLMediaKit/release/linux/Release"
RUN_DIR="$ROOT/build/linux/avox/install/x86_64"
CFG=/tmp/zlm_config.ini
HTTP=8080; RTMP=11935; RTSP=8554
SECRET=035c73f7-bb6b-4889-a715-d9eb2d1925cc

# 高端口配置副本 (80/554/1935 是特权端口且 80 常被占)
# 注意: config.ini 由 Windows 侧签出, 带 \r, 先剥掉再 sed 否则 $ 锚点匹配不上
tr -d '\r' < "$ZLM_DIR/config.ini" | sed \
    -e "s/^port=80\$/port=$HTTP/" \
    -e "s/^port=1935\$/port=$RTMP/" \
    -e "s/^port=554\$/port=$RTSP/" \
    -e "s/^sslport=443\$/sslport=18443/" \
    > "$CFG"

# 清理可能残留的 MediaServer/推流
pkill -f "MediaServer -c $CFG" 2>/dev/null || true
python3 "$ROOT/script/testenv/push_streams.py" --stop 2>/dev/null || true
sleep 1

# 起 MediaServer
"$ZLM_DIR/MediaServer" -c "$CFG" > /tmp/zlm.log 2>&1 &
ZLM_PID=$!
trap "kill $ZLM_PID 2>/dev/null; python3 '$ROOT/script/testenv/push_streams.py' --stop --api http://127.0.0.1:$HTTP 2>/dev/null" EXIT
sleep 2
if ! kill -0 $ZLM_PID 2>/dev/null; then
  echo "[error] MediaServer 启动失败:"; tail -5 /tmp/zlm.log; exit 1
fi
# MediaServer 加载时会回写配置, secret 以回写后的为准 (防止随机再生)
CFG_SECRET=$(grep -m1 "^secret=" "$CFG" | cut -d= -f2 | tr -d '\r')
[ -n "$CFG_SECRET" ] && SECRET=$CFG_SECRET
echo "[info] MediaServer pid=$ZLM_PID http=$HTTP rtsp=$RTSP rtmp=$RTMP"

# 推流 (h264 走 rtmp/flv; h265 检查后未就绪则走 rtsp push 兜底:
# 系统 ffmpeg 6.x 不支持 hevc-in-flv, ZLM rtsp push 后各协议自动转出)
export ZLM_SECRET=$SECRET
python3 "$ROOT/script/testenv/push_streams.py" --api http://127.0.0.1:$HTTP --secret "$SECRET" || true
H265_UP=$(python3 - <<EOF
import json,urllib.request,urllib.parse
u="http://127.0.0.1:$HTTP/index/api/getMediaList?secret=$SECRET"
try:
    d=json.loads(urllib.request.urlopen(u,timeout=3).read())
    print(1 if any(m.get("app")=="live" and m.get("stream")=="avox" for m in d.get("data") or []) else 0)
except Exception:
    print(0)
EOF
)
if [ "$H265_UP" != "1" ]; then
  echo "[info] h265 flv push 未就绪, 走 rtsp push 兜底"
  ffmpeg -hide_banner -loglevel error -nostdin -re -stream_loop -1 \
    -i "$ROOT/assets/video/test/test_h265_aac_960x540.mp4" -c copy \
    -f rtsp "rtsp://127.0.0.1:$RTSP/live/avox" > /tmp/h265_push.log 2>&1 &
  H265_PID=$!
  trap "kill $ZLM_PID $H265_PID 2>/dev/null; python3 '$ROOT/script/testenv/push_streams.py' --stop --api http://127.0.0.1:$HTTP 2>/dev/null" EXIT
  sleep 3
fi

# 全量矩阵 (webrtc 库 Linux 未编; yuvout-h264 是真机硬解哨兵; shot 车道B;
# rec-transcode 需视频编码器 —— 均为 WSL/桌面Linux 结构性跳过)
EXTRA_SKIP="webrtc-h264,webrtc-h265,frame-contract,shot,yuvout-h264-soft,yuvout-h264,rec-transcode-h264,rec-transcode-novk"
cd "$RUN_DIR"
LD_LIBRARY_PATH="$RUN_DIR" ./playtest \
  --host=127.0.0.1 --http=$HTTP --rtmp=$RTMP --rtsp=$RTSP \
  --skip="$EXTRA_SKIP${1:+,$1}" \
  --outdir="$ROOT/build/playmatrix_out" 2>&1 | grep -E "\[AVOX\]\[TEST\]"
