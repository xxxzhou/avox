# A-1 ASS/PGS 链收尾

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T1/T2 完成, T3 延迟接口已落地; 剩 ASS 轨样式覆盖/候选枚举/PGS) · 来源:backlog A-1
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
- [x] T2 编码自愈接线 + 信号暴露(2026-09-19, `1608887`): `normalizeSubtitleText` bool→
      `SubtitleEncoding` 枚举 {unknown=0, utf8, utf8BomStripped, gbkTranscoded, utf16Raw},
      修 UTF-16 歧义坑; 信号经 `ISubtitle::getFileEncoding()`(带默认实现, 只增不改)
      透出, SubtitleFile 记录/卸载复位。**落点偏离记录**: 原定「getSubtitleDesc 扩字段」
      对外挂不适用(外挂不经源), 改走 ISubtitle 查询; ISTrackDesc 未动; SWIG 四语言为
      构建期再生成件(gitignore), 头文件即真源。ass 插件路径(.ass/.ssa)编码自愈与探测
      未做(插件不链 avox 核心, 需先解决 normalize 可达性, 新增缺口)。
- [ ] T3 样式参数补齐: **延迟接口已落地(2026-09-19, `21491dc`)**: `ISubtitle::setDelay(ms)`
      (正=延后/负=提前, 带默认实现), 外挂文本/内封 ASS 按 (pts-delay) 平移内容选择,
      PGS 画布按 pts 到期放行(delay=0 原路径零变化), ASR 实时口播不平移。
      剩: ASS 轨缩放/字体覆盖接口。现有全局变换 scale/offset/opacity 三层通用
      (`SubtitleView.cpp:252`),叠加轨级覆盖;依据 `doc/plan/player/字幕样式设计.md`
      (v3 定稿)的生效矩阵。
- [ ] T4 PGS 真实样片 e2e:找/造含 PGS 的 mkv(ffmpeg 不能编 PGS,需真实样本),进 avox-test 资产。
- [ ] T5 中文外挂自动加载探测:同名候选枚举接口(`movie.zh.srt/.chs.ass/.gbk.srt` 等常见命名
      归一化),引擎只列候选、产品决定加载。
      **设计定稿(2026-09-19 夜, 未动代码, 实施前过目 API 形状)**:
      - 语义: 产品拿到视频 URL 后调一次枚举, 引擎在同目录扫「同主名」字幕文件,
        归一化后按优先级排序返回; 只列候选不加载, 加载仍走 loadSubtitle。
      - 命名归一: 主名 = 视频文件名去扩展名; 候选 = 同目录下「主名开头」且扩展名
        `.srt/.ass/.ssa` 的文件; 语言标记段(`.zh/.chs/.cht/.gb/.big5/.eng` 等)与
        `.gbk` 编码后缀解析为 hint 字段, 不参与主名匹配。
      - 排序: 主名完全同名 > 带语言标记; srt > ass(中文场景 srt 命中率高, 可再议)。
      - API 形状(公共头无 STL 约束, 参照 ISTrackDesc 口径):
        `ISubtitle` 追加(只增不改, 带默认实现):
        `virtual int32_t listSubtitleCandidates(const char* videoUrl,
        SubtitleCandidate* out, int32_t cap) { return 0; }`;
        `SubtitleCandidate { char path[512]; SCodecId codec; }` 定义于 AvoxPlayer.h;
        返回值=实得个数(可 >cap 截断, 负=错误)。纯查询, 不持有文件句柄。
      - SWIG: 结构体定长数组跨语言自动映射, 四语言随构建再生成。
      - 工作量: 引擎扫描+归一化半天(纯文件系统操作, 无平台差异), 用例归 avox-test。

## 验收

- avox-test playmatrix G 组全绿 + 像素取证(暗底亮字判据)。
- GBK/BOM 文件加载后字幕可读,且探测信号经 FFI 可见(panvox P-5 字幕体验包吃这个接口)。

## 风险与开放问题

- libass 真渲染绑定 `AVOX_ASS_DEPS_DIR` 产物,验收环境需先跑 `script/ass/build_windows.py` 固定产物。
- 时序竞态回归点:选轨时 extradata 未就绪窗口(`MediaPlayer.cpp:627`)与 pendingSubs 回放(:565)。
- 样式/信号接口动 `ISubtitle` ABI,SWIG 四语言(C#/Java/Node/python)同步是隐性工作量。
