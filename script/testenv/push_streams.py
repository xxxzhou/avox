#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""本地测试流源套件: 复用已运行的 ZLMediaKit MediaServer, ffmpeg 循环推流出
rtsp/rtmp/http-flv/hls 多协议源, 供三引擎功能测试与自动化回归使用。
每路就绪后输出 [AVOX][TEST] 判定行, 可被 collect_verdicts.py 汇总。

用法:
  python script/testenv/push_streams.py             # 推默认流 live/avox (avox_electron.mp4)
  python script/testenv/push_streams.py --all       # 推 assets/video 全部样本
  python script/testenv/push_streams.py --status    # 查看 MediaServer 当前媒体
  python script/testenv/push_streams.py --stop      # 停掉本脚本推的流 (kill PID + close_stream 兜底)

secret 查找顺序: --secret > 环境变量 ZLM_SECRET > 常见 MediaServer config.ini 嗅探。
注意: 手机等局域网设备拉流时 host 用本机局域网 IP, 不是 127.0.0.1 (见 --lan-ip)。
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PID_FILE = Path(__file__).with_name(".push_pids.json")
CREATE_NO_WINDOW = 0x08000000  # windows: 不弹 ffmpeg 控制台

CONFIG_CANDIDATES = [
    r"D:\Work\github\ZLMediaKit\release\windows\Debug\Release\config.ini",
    r"D:\Work\github\ZLMediaKit\release\windows\Release\Release\config.ini",
    r"D:\Work\github\ZLMediaKit\release\linux\Debug\config.ini",
    r"C:\Work\github\ZLMediaKit\release\windows\Debug\Release\config.ini",
]


def http_get_json(url: str, timeout: float = 5.0) -> dict:
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def zlm_api(api: str, secret: str, action: str, params: dict = None) -> dict:
    q = {"secret": secret}
    if params:
        q.update(params)
    url = f"{api}/index/api/{action}?" + urllib.parse.urlencode(q)
    d = http_get_json(url)
    if d.get("code") != 0:
        raise RuntimeError(f"{action} 失败: code={d.get('code')} msg={d.get('msg')}")
    return d


def find_secret(explicit: str) -> str:
    if explicit:
        return explicit
    env = os.environ.get("ZLM_SECRET")
    if env:
        return env
    for c in CONFIG_CANDIDATES:
        p = Path(c)
        if p.is_file():
            m = re.search(r"\[api\][^\[]*?secret\s*=\s*(\S+)",
                          p.read_text(encoding="utf-8", errors="replace"), re.S)
            if m:
                print(f"secret 嗅探自 {p}")
                return m.group(1)
    raise SystemExit("未找到 ZLM secret: 传 --secret 或设 ZLM_SECRET 环境变量")


def zlm_ports(api: str, secret: str) -> dict:
    c = zlm_api(api, secret, "getServerConfig")["data"][0]
    return {
        "http": int(c.get("http.port", 80) or 80),
        "rtsp": int(c.get("rtsp.port", 554) or 554),
        "rtmp": int(c.get("rtmp.port", 1935) or 1935),
    }


def media_list(api: str, secret: str) -> list:
    return zlm_api(api, secret, "getMediaList").get("data") or []


def verdict(case: str, ok: bool, detail: str = "") -> None:
    tail = f" {detail}" if detail else ""
    print(f"[AVOX][TEST] case={case} result={'PASS' if ok else 'FAIL'}{tail}")


def play_urls(host: str, ports: dict, key: str) -> dict:
    """各 schema 的拉流 URL 模板 (均已实测; fmp4 是 WebRTC 内部 schema 无公开模板)"""
    app, stream = key.split("/", 1)
    http = f"http://{host}:{ports['http']}"
    return {
        "RTSP": f"rtsp://{host}:{ports['rtsp']}/{key}",
        "RTMP": f"rtmp://{host}:{ports['rtmp']}/{key}",
        "HLS": f"{http}/{app}/{stream}/hls.m3u8",
        "TS": f"{http}/{app}/{stream}.live.ts",
        "WebRTC": f"{http}/index/api/webrtc?app={app}&stream={stream}&type=play",
    }


def wait_ready(api: str, secret: str, keys: list, timeout_s: float = 20.0) -> dict:
    """轮询直到所有 key 出现; 返回 {key: set(schema)}"""
    deadline = time.time() + timeout_s
    found = {}
    while time.time() < deadline:
        found = {}
        for m in media_list(api, secret):
            found.setdefault(f"{m.get('app')}/{m.get('stream')}", set()).add(m.get("schema"))
        if all(k in found for k in keys):
            return found
        time.sleep(1.0)
    return found


def cmd_push(args) -> int:
    secret = find_secret(args.secret)
    api = args.api
    zlm_api(api, secret, "getServerConfig")  # 健康检查
    ports = zlm_ports(api, secret)
    ffmpeg = args.ffmpeg or shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("未找到 ffmpeg, 传 --ffmpeg")
    host = args.lan_ip or "127.0.0.1"
    # 默认两路: H265(avox_electron) + H264(webrtc_pull), 编码覆盖开箱即得; --all 推全部样本
    if args.all:
        pairs = [(f, f.stem) for f in sorted(REPO_ROOT.glob("assets/video/*.mp4"))]
    else:
        pairs = [(REPO_ROOT / "assets/video/avox_electron.mp4", "avox"),
                 (REPO_ROOT / "assets/video/webrtc_pull.mp4", "avox264")]
    missing = [str(f) for f, _ in pairs if not f.is_file()]
    if missing:
        raise SystemExit(f"样本缺失: {missing}")
    pushes = []
    for f, stem in pairs:
        name = f"live/{args.prefix}{stem}"
        rtmp_url = f"rtmp://127.0.0.1:{ports['rtmp']}/{name}"
        proc = subprocess.Popen(
            [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
             "-re", "-stream_loop", "-1", "-i", str(f), "-c", "copy",
             "-f", "flv", rtmp_url],
            creationflags=CREATE_NO_WINDOW if os.name == "nt" else 0)
        pushes.append({"file": str(f), "stream": name, "pid": proc.pid})
        print(f"push {f.name} -> {rtmp_url} (pid={proc.pid})")
    PID_FILE.write_text(json.dumps({"api": api, "pushes": pushes},
                                   ensure_ascii=False, indent=1), encoding="utf-8")
    keys = [p["stream"] for p in pushes]
    found = wait_ready(api, secret, keys)
    ok_all = True
    for p in pushes:
        key = p["stream"]
        schemas = found.get(key, set())
        urls = play_urls(host, ports, key)
        if not schemas:
            verdict(f"push-{key.replace(chr(47), chr(45))}", False, "推流未就绪(20s)")
            ok_all = False
            continue
        for schema in sorted(schemas):
            u = urls.get(str(schema).upper())
            if u:
                verdict(f"push-{key.replace(chr(47), chr(45))}-{schema}", True, f"url={u}")
            else:
                print(f"  schema {schema} (无固定拉流 URL 模板)")
    # HLS 默认按需生成: 对没起来的 key 各拉一次触发
    missing_hls = [k for k in keys if "HLS" not in found.get(k, set())]
    if missing_hls:
        for k in missing_hls:
            try:
                urllib.request.urlopen(play_urls(host, ports, k)["HLS"], timeout=5).read(64)
            except Exception:
                pass
        found = wait_ready(api, secret, missing_hls, timeout_s=10.0)
        for k in missing_hls:
            if "HLS" in found.get(k, set()):
                verdict(f"push-{k.replace(chr(47), chr(45))}-HLS", True,
                        f"url={play_urls(host, ports, k)['HLS']}")
    return 0 if ok_all else 1


def cmd_stop(args) -> int:
    if not PID_FILE.is_file():
        print("无推流记录 (.push_pids.json 不存在)")
        return 0
    rec = json.loads(PID_FILE.read_text(encoding="utf-8"))
    for p in rec.get("pushes", []):
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p["pid"])],
                           capture_output=True)
        else:
            subprocess.run(["kill", str(p["pid"])], capture_output=True)
        print(f"killed pid={p['pid']} ({p['stream']})")
    # close_stream 兜底清残留 (ZLM 要求 schema/vhost 全给)
    try:
        secret = find_secret(args.secret)
        for p in rec.get("pushes", []):
            app, stream = p["stream"].split("/", 1)
            for schema in ("rtsp", "rtmp", "hls", "ts", "fmp4"):
                try:
                    zlm_api(rec["api"], secret, "close_stream",
                            {"app": app, "stream": stream, "schema": schema,
                             "vhost": "__defaultVhost__", "force": 1})
                except Exception:
                    pass
    except Exception as e:
        print(f"close_stream 兜底跳过: {e}")
    PID_FILE.unlink(missing_ok=True)
    verdict("push-stop", True, f"{len(rec.get('pushes', []))} 路")
    return 0


def cmd_status(args) -> int:
    items = media_list(args.api, find_secret(args.secret))
    if not items:
        print("MediaServer 无在线媒体")
        return 0
    for m in items:
        print(f"{m.get('app')}/{m.get('stream')}  schema={m.get('schema')}"
              f"  encoder={m.get('encoder_id', '')}  readers={m.get('readerCount', 0)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="ZLM 本地测试流源套件")
    ap.add_argument("--api", default="http://127.0.0.1", help="ZLM http API 地址")
    ap.add_argument("--secret", default="", help="ZLM api secret (默认嗅探 config.ini)")
    ap.add_argument("--ffmpeg", default="", help="ffmpeg 路径 (默认 PATH)")
    ap.add_argument("--lan-ip", default="", help="判定行里输出的主机地址 (手机拉流时填本机局域网 IP)")
    ap.add_argument("--prefix", default="", help="流名前缀")
    ap.add_argument("--all", action="store_true", help="推 assets/video 全部样本")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--stop", action="store_true", help="停掉本脚本推的流")
    g.add_argument("--status", action="store_true", help="查看当前媒体列表")
    args = ap.parse_args()
    if args.stop:
        return cmd_stop(args)
    if args.status:
        return cmd_status(args)
    return cmd_push(args)


if __name__ == "__main__":
    sys.exit(main())
