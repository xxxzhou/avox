# GN编译

## 文档

[OpenHarmony开发——GN快速上手](https://zhuanlan.zhihu.com/p/679846981)

## 命令

// 查看完整模块名
gn ls ../build/windows/release | findstr /i "builtin_video_decoder_factory"
// 查看模块依赖
gn desc ../build/windows/release //api/video_codecs:builtin_video_decoder_factory deps
// 查找引用指定模块的其他模块
gn refs ../build/windows/release //api/video_codecs:builtin_video_decoder_factory
// 查看指定模块的所有依赖
gn desc ../build/windows/release //api/video_codecs:builtin_video_decoder_factory all_deps
// 查看哪些模块依赖了指定模块
gn desc ../build/windows/release //api/video_codecs:builtin_video_decoder_factory all_includers

// 查找
gn ls ../build/windows/release | findstr /i "webrtc"
//:webrtc
gn desc ../build/windows/release //:webrtc deps