# FFmpeg

## 文档

[在Windows上编译FFmpeg库](https://zhuanlan.zhihu.com/p/16550406805)

[window10_ffmpeg-msys2-msvc编译](https://www.xianwaizhiyin.net/?p=212)

[ffmpeg-cmake](https://github.com/Pawday/ffmpeg-cmake)

[ffmpeg-arcana-cmake](https://github.com/richinsley/ffmpeg-arcana-cmake)

[ffmpeg.wasm](https://github.com/ffmpegwasm/ffmpeg.wasm)

[ffmpeg-builds](https://github.com/BtbN/FFmpeg-Builds/releases)

[一篇文章助你入门FFmpeg编程](https://zhuanlan.zhihu.com/p/696614773)

## 命令

### 推RTSP流

ffmpeg -re -i D:/Back/tt.mp4 -vcodec h264 -acodec aac -f rtsp -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -i D:/Back/tt_265.mp4 -vcodec libx265 -acodec aac -f rtsp -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/tt.mp4 -c copy -f rtsp rtsp://127.0.0.1:554/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/美好.mp4 -c copy -f rtsp  rtsp://127.0.0.1:554/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/美好_h265.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 



// 这种用zlmediakit拉流，其PTS比原始扩大大约9倍
ffmpeg -re -stream_loop -1 -i D:/Back/美好.mp4 -c copy -f rtsp -flush_packets 1 -fflags +genpts -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/tt.mp4 -c copy -f rtsp -flush_packets 1 -fflags +genpts -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/142304.mp4 -c copy -f rtsp  rtsp://127.0.0.1:554/live/test

// rtmp需要强制使用flv格式
ffmpeg -re -stream_loop -1 -i D:/Back/xingtong.mp4 -c copy -f flv rtmp://127.0.0.1/live/test
ffmpeg -re -stream_loop -1 -i D:/Back/xingtong_h265.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/美好_s16.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/美好_s16_mono.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -i rtsp://192.168.1.100:554/live/0123456789ab_0 -c copy -f rtsp rtsp://127.0.0.1:554/live/test
ffmpeg -re -stream_loop -1 -i D:/Back/tt1.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/美好02.mp4 -c copy -f rtsp -flush_packets 1 -fflags +genpts -rtsp_transport tcp rtsp://127.0.0.1/live/test
ffmpeg -re -stream_loop -1 -i D:/Back/美好02.mp4 -c copy -f flv rtmp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/tt1.mp4 -c copy -f flv rtmp://127.0.0.1/live/test

ffprobe -show_streams rtsp://127.0.0.1/live/test

ffmpeg -re -i D:/Back/tt.mp4 -vcodec h264 -bf 0 -acodec aac -f flv rtmp://127.0.0.1/live/test

ffmpeg -re -i D:/Back/tt.mp4 -c:v libx264 -preset ultrafast -tune zerolatency -b:v 1000k -c:a aac -b:a 128k -f webrtc -sdp_file output.sdp webrtc://example.com:8080/stream

// 软硬解测试
ffmpeg -i D:/Back/tt.mp4 -c:v libx265 D:/Back/tt_outx265.mp4

ffmpeg -hwaccel qsv -i D:/Back/tt.mp4 -c:v hevc_qsv D:/Back/tt_outx265.mp4

ffmpeg -hwaccel qsv -i D:/Back/tt.mp4 -c:v h264_qsv D:/Back/tt_outx264.mp4

ffmpeg -hwaccel qsv -i D:/Back/tt_265.mp4 -c:v h264_qsv D:/Back/tt_outx264.mp4

### 查看多少I帧

ffmpeg -i D:/0.mp4 -vf "select='eq(pict_type,I)',showinfo" -f null -

### 推RTMP流

ffmpeg -re -i D:/Back/tt.mp4 -vcodec libx264 -acodec aac -f flv rtmp://127.0.0.1/live/test

ffmpeg -re -i D:/young-woman.flv -vcodec libx264 -acodec aac -f flv rtmp://127.0.0.1/live/test

## 查看当前ffmpeg支持的硬件解码

cd D:\Work\github\avox\build\windows\ffmpeg\bin

ffmpeg -hwaccels

ffmpeg -decoders >> decoders.txt

// 列出所有H264相关解码器
ffmpeg -hide_banner -decoders | findstr h264

## H264文件转H265

ffmpeg -i 美好.mp4 -c:v libx265 -x265-params "b-adapt=0:bframes=0" -c:a copy 美好_h265.mp4

ffmpeg -i 美好_s16.mp4 -c:v copy -ac 1 -c:a aac 美好_s16_mono.mp4

ffmpeg -i xingtong.mp4 -c:v libx265 -x265-params "b-adapt=0:bframes=0" -c:a copy xingtong_h265.mp4

## 音频重采样 

// 由aac float转s16,44100转16000
ffmpeg -i input.mp4 -c:v copy -af "aformat=sample_fmts=s16" -c:a aac -ar 16000 -ac 2 xingtong_s16.mp4

## 去掉B帧

ffmpeg -i xingtong.mp4 -c:v libx265 -x265-params "b-adapt=0:bframes=0" -c:a copy xingtong_nob.mp4