# 播放

## 设计

现在render/decoder可以脱离，不过player-track-rendertask|decodertask算是绑定在一起，如果要分开，主要是player里的IO包队列，track里的decoder要使用，而track的decoder解码的线程在帧列队了，其render要使用帧列队，导致了强绑定关系。二是有些共用对象，如记录埋点，播放器自身时钟，音频与视频同步时钟都需要联系。暂时还找不到分离的强需求，现在render/decoder已分开，其SourcePlayer也可利用render快速建立一个播放器，WebRTC可利用decoder打通各平台的h264/h265的硬解及render渲染到原生窗口。

