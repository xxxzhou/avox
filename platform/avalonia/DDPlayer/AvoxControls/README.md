# 功能实现

## 主要类

AvPlayerControl: 播放器控制类,包含播放器的渲染窗口与播放器控制,包含开始/暂停/停止/快进/快退/音量控制/截图等功能.

AvRenderView: 封装播放器的平台对应原生渲染窗口,用于渲染视频帧.

AvPlayerModel: 播放器模型类,包含播放器的播放状态等状态.

SettingControl: 设置控制类,包含设置界面的控件,打开网络源/本地文件,播放硬解IO选项设置,媒体信息,水印设置等.

AvPlayerView: 播放器视图类,组合AvRenderView,AvPlayerControl,SettingControl.

## 播放截图

基于AvPlayer里的ISurfaceRender接口,接口实现screenShot,用于填充一个createImageBuffer创建IImageBuffer对象,此对象对应ISurfaceRender正在渲染的完整的原始图像,IImageBuffer使用ImageUtils保存成PNG对象.

## 音量控制

基于AvPlayer里的 IAudioRender接口,接口实现getVolume/setVolume,用于得到及设置播放音量.

## 变速播放

基于AvPlayer里的void SetSpeed(double speed),用于设置播放速度.