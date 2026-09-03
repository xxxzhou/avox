# 测试

## ffmpeg推流

// rtmp需要强制使用flv格式
ffmpeg -re -stream_loop -1 -i D:/Back/xingtong.mp4 -c copy -f flv rtmp://127.0.0.1/live/test
ffmpeg -re -stream_loop -1 -i D:/Back/xingtong_h265.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/美好_s16.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/美好_s16_mono.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 
ffmpeg -re -stream_loop -1 -i D:/Back/tt1.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 

// 这种用zlmediakit拉流，其PTS比原始扩大大约9倍
ffmpeg -re -stream_loop -1 -i D:/Back/美好.mp4 -c copy -f rtsp -flush_packets 1 -fflags +genpts -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/tt.mp4 -c copy -f rtsp -flush_packets 1 -fflags +genpts -rtsp_transport tcp rtsp://127.0.0.1/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/142304.mp4 -c copy -f rtsp  rtsp://127.0.0.1:554/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/tt.mp4 -c copy -f rtsp rtsp://127.0.0.1:554/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/美好.mp4 -c copy -f rtsp  rtsp://127.0.0.1:554/live/test

ffmpeg -re -stream_loop -1 -i D:/Back/美好_h265.mp4 -c:v copy -c:a copy -f rtsp rtsp://127.0.0.1:554/live/test 