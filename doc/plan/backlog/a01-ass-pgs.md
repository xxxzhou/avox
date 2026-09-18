# A-1 ASS/PGS 链收尾

> 状态: 进行中 · 上次核对: 2026-09-18 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T1 完成, T2 接线已落、信号暴露未做) · 来源:backlog A-1
内封 ASS 已像素级验收(2026-09-15),本计划覆盖剩余四件 + 乱码探测信号暴露。

## 出口判据

1. 外挂 ASS/SRT(UTF-8/BOM/GBK/富文本)加载渲染全绿(avox-test playmatrix G 组字幕矩阵)。
2. ASS 轨独立样式参数(缩放/延迟/字体)接口可用,中文文档口径与字幕样式 v3 定稿一致。
3. PGS 真实样片 e2e 通过(位图字幕上屏取证)。
4. 中文外挂自动加载:同名候选探测接口可查,产品可决定加载。
5. 编码探测信号从引擎暴露(产品能知道「这个文件是 GBK,已自愈」)。

## 现状(代码落点)

- **接口面已齐**:`ISubtitle` 三槽位仲裁(内封/外挂/ASR,后激活者胜)见
  `src/avox/AvoxPlayer.h:29-60`、`src/avox/subtitle/SubtitleSlots.hpp:12-45`;
  `loadSubtitle/setSubtitleTrack/unloadSubtitle` 在 `AvoxPlayer.h:216-224`,
  实现走命令队列 `src/avox/player/MediaPlayer.cpp:461-581`。
- **渲染**:libass 封装插件 `plugins/avox_ass/AssOverlay.cpp`(init :20-37、内封 extradata :73、
  chunk :81、srt→ass 转换 :92-132);真渲染依赖预编译产物 `AVOX_ASS_DEPS_DIR`
  (缺失时骨架降级不崩,`plugins/avox_ass/CMakeLists.txt:14-34`)。
- **PGS 全链已通无 TODO**:`src/avox_ffmpeg/PgsDecoder.cpp`(pal8→RGBA :15)、
  IO 路由 `IOParseFF.cpp:126-140,256-271`、视图 `SubtitleView.cpp:376 setPgsCanvas`。
- **编码自愈已接线(2026-09-18 复核)**:`SubtitleFile::loadFile` 已接 `normalizeSubtitleText`
  (CharsetConvert, GBK→UTF-8 转换 + UTF-8 剥 BOM),G 组 bom/gbk 用例随此转绿;
  剩探测**信号暴露**给产品(枚举/回调,见 T2,待拍板)。
- **验证基建已在**:avox-test `l1_avox/playmatrix` G 组字幕矩阵(外挂 SRT 三编码/外挂 ASS/
  内嵌 SRT/ASS/mov_text,亮像素取证),素材生成 `avox-test/assets/gen/gen_subtitle.py`。

## 任务拆解

- [x] T1 外挂加载 e2e 跑绿(2026-09-18):playmatrix G 组离线 9/9 绿(sub-ext-srt-utf8/bom/gbk/
      rich/ass、sub-style-srt、sub-embed-srt/ass/movtext,亮像素取证);BOM 剥离收在
      `SubtitleFile::loadFile` 的 `normalizeSubtitleText` 内完成。
- [ ] T2 编码自愈接线 + 信号暴露:接线已落(见现状);剩探测结果做成
      枚举(utf8/gbk/transcoded)随加载结果返回;新回调或 desc 字段暴露给产品
      (候选:方案 A `normalizeSubtitleText` 返枚举 + `SubtitleDesc` 扩字段[推荐],或方案 B
      `ISubtitle` 加 onEncodingDetected 回调——动 ABI 需同步 SWIG 四语言,待晨会拍板)。
- [ ] T3 样式参数补齐:延迟参数全链没有(grep 无 setDelay);ASS 轨缩放/字体覆盖接口。
      现有全局变换 scale/offset/opacity 三层通用(`SubtitleView.cpp:252`),
      叠加轨级覆盖;依据 `doc/plan/player/字幕样式设计.md`(v3 定稿)的生效矩阵。
- [ ] T4 PGS 真实样片 e2e:找/造含 PGS 的 mkv(ffmpeg 不能编 PGS,需真实样本),进 avox-test 资产。
- [ ] T5 中文外挂自动加载探测:同名候选枚举接口(`movie.zh.srt/.chs.ass/.gbk.srt` 等常见命名
      归一化),引擎只列候选、产品决定加载。

## 验收

- avox-test playmatrix G 组全绿 + 像素取证(暗底亮字判据)。
- GBK/BOM 文件加载后字幕可读,且探测信号经 FFI 可见(panvox P-5 字幕体验包吃这个接口)。

## 风险与开放问题

- libass 真渲染绑定 `AVOX_ASS_DEPS_DIR` 产物,验收环境需先跑 `script/ass/build_windows.py` 固定产物。
- 时序竞态回归点:选轨时 extradata 未就绪窗口(`MediaPlayer.cpp:627`)与 pendingSubs 回放(:565)。
- 样式/信号接口动 `ISubtitle` ABI,SWIG 四语言(C#/Java/Node/python)同步是隐性工作量。
