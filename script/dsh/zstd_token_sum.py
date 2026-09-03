#!/usr/bin/env python3
import zstandard as zstd
import json
import sys
import os

def accumulate_from_zstd(path):
    if not os.path.exists(path):
        print(f"not found: {path}")
        return (0, 0, 0)
    crt_cache = 0
    crt_input = 0
    crt_output = 0
    with open(path, "rb") as f:
        dctx = zstd.ZstdDecompressor()
        reader = dctx.stream_reader(f)
        text = reader.read().decode("utf-8", errors="replace")
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except:
            continue
        # usage can be in:
        # 1. assistant/chunk: data.chunk.usage
        if (
            isinstance(obj, dict)
            and obj.get("type") == "assistant/chunk"
            and "data" in obj
            and isinstance(obj["data"], dict)
            and "chunk" in obj["data"]
            and isinstance(obj["data"]["chunk"], dict)
        ):
            chunk = obj["data"]["chunk"]
            if "usage" in chunk and isinstance(chunk["usage"], dict):
                usage = chunk["usage"]
                crt_cache += usage.get("cacheReadTokens", 0)
                crt_input += usage.get("inputTokens", 0)
                crt_output += usage.get("outputTokens", 0)
        # 2. assistant/message: data.usage
        elif (
            isinstance(obj, dict)
            and obj.get("type") == "assistant/message"
            and "data" in obj
            and isinstance(obj["data"], dict)
            and "usage" in obj["data"]
            and isinstance(obj["data"]["usage"], dict)
        ):
            usage = obj["data"]["usage"]
            crt_cache += usage.get("cacheReadTokens", 0)
            crt_input += usage.get("inputTokens", 0)
            crt_output += usage.get("outputTokens", 0)
    return (crt_cache, crt_input, crt_output)

def main():
    root = r"C:\Users\mfjt5\.dsh\sessions"
    paths = [
        os.path.join(root, "--D-Work-github-avox--",
                    "session-38499128-2fac-4e75-ba8f-006cc16059c8", "session.jsonl.zstd"),
        os.path.join(root, "--D-Work-github-deepseek-harness--",
                    "session-adcffb14-c735-46b8-b9e8-f32d8962d248", "session.jsonl.zstd"),
    ]
    total_cache = 0
    total_input = 0
    total_output = 0
    for path in paths:
        cache, input_, output = accumulate_from_zstd(path)
        print(f"{os.path.basename(os.path.dirname(path))}: cache {cache}  input {input_}  output {output}")
        total_cache += cache
        total_input += input_
        total_output += output
    print(f"\n合计：缓存命中 {total_cache} / 未命中 {total_input} / 输出 {total_output}")

if __name__ == "__main__":
    main()
