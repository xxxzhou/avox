@echo off
set WEBRTC_DIR=D:\Work\webrtc
rem 切换到WebRTC目录: %WEBRTC_DIR%
cd /d %WEBRTC_DIR%
set vs2022_install=C:\Program Files\Microsoft Visual Studio\2022\Community
set GYP_GENERATORS=msvs-ninja,ninja
set WINDOWSSDKDIR=C:\Program Files (x86)\Windows Kits\10
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
fetch --nohooks webrtc
gclient sync
