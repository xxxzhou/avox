#!/bin/bash
# Windows 侧驱动: 分块跑 WSL 增量构建, 规避长会话必死问题
LOG=/home/zhouxin/avox/build19.log
for i in $(seq 1 20); do
  wsl.exe -d Ubuntu -u root -- bash -c 'swapon /swapfile 2>/dev/null; true' > /dev/null 2>&1
  echo "=== CHUNK $i START $(date +%H:%M:%S) ===" >> /d/wslbuild_chunk.log
  wsl.exe -d Ubuntu -- bash -c "cd ~/avox && CMAKE_BUILD_PARALLEL_LEVEL=1 timeout -s KILL 660 python3 -u build_linux.py >> $LOG 2>&1; echo CHUNK_EXIT=\$? >> $LOG" > /dev/null 2>&1
  RC=$(wsl.exe -d Ubuntu -- bash -c "grep -o 'BUILD_RC=[0-9]*' $LOG 2>/dev/null | tail -1" 2>/dev/null | tr -d '\r\n\0')
  echo "=== CHUNK $i END rc=$RC ===" >> /d/wslbuild_chunk.log
  if [ "$RC" = "BUILD_RC=0" ]; then
    echo "BUILD_OK"
    break
  fi
  sleep 5
done
