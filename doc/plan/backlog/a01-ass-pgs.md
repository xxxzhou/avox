# A-1 ASS/PGS 链收尾

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T1/T2/T3延迟/T5候选枚举/ass路径自愈 完成; 剩 ASS 轨样式覆盖 + PGS e2e 素材合成) · 来源:backlog A-1
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
      构建期再生成件(gitignore), 头文件即真源。~~ass 插件路径(.ass/.ssa)编码自愈与
      探测未做(插件不链 avox 核心, 需先解决 normalize 可达性, 新增缺口)~~
      **缺口已修(2026-09-19, 见 T5 提交)**: CharsetConvert.cpp 直编入 avox_ass 插件
      (无 avox 内部依赖), loadFile 双自愈(内容 normalizeSubtitleText + 路径
      openFileUtf8), 编码经 IAssOverlay::getFileEncoding(只增不改)回传 SubtitleView。
      ⚠️ **契约变更**: 「.ass 插件路径不探测仍为 unknown」作废 —— 现在 .ass/.ssa
      加载后 getFileEncoding 返回真实探测(GBK 也能转码加载); UTF-16 文件渲染不可用
      但编码可查(与 srt 同口径)。avox-test `sub-enc-expose` 契约行需同步,
      可加 `sub_enc_ass_gbk` 用例吃新行为。
- [ ] T3 样式参数补齐: **延迟接口已落地(2026-09-19, `21491dc`)**: `ISubtitle::setDelay(ms)`
      (正=延后/负=提前, 带默认实现), 外挂文本/内封 ASS 按 (pts-delay) 平移内容选择,
      PGS 画布按 pts 到期放行(delay=0 原路径零变化), ASR 实时口播不平移。
      剩: ASS 轨缩放/字体覆盖接口。现有全局变换 scale/offset/opacity 三层通用
      (`SubtitleView.cpp:252`),叠加轨级覆盖;依据 `doc/plan/player/字幕样式设计.md`
      (v3 定稿)的生效矩阵。
- [ ] T4 PGS 真实样片 e2e:**卡点已解(2026-09-19 定配方)—— 无需真实样片, 自合成**。
      avox-test 侧加 `gen_pgs_asset.py`(script/testenv) 即可造素材:
      - .sup 段格式(FFmpeg supdemuxer 口径, 已核 supdec.c): `"PG"`(u16) + PTS(u32 BE,
        90kHz) + DTS(u32 BE, 可=PTS) + type(u8) + size(u16 BE) + payload; 探测需 ≥4 个
        连续合法段。
      - 段类型: 0x14 PDS(调色板, 2-4 色即可)/0x15 ODS(对象 RLE 位图, 用纯色矩形,
        亮像素取证友好)/0x16 PCS(展示组合)/0x17 WDS(窗口)/0x80 END。
      - 事件: 显示 = PCS+PDS+ODS+WDS+END, 消除 = 空 PCS+END; 挂到已知 PTS
        (如 1s 出 4s 收)。
      - 封装: `ffmpeg -i <现有测试视频.h264/ts> -i gen.sup -map 0 -map 1 -c copy
        out_pgs.mkv`(PGS 只进 mkv)。
      - 引擎侧链路已全通(PgsDecoder/IOParseFF 路由/setPgsCanvas), 素材就位即 e2e。
      - 用例需求: `sub-pgs-e2e`(播放亮像素取证) + `sub-pgs-seek`(seek 后仍上屏)。
- [x] T5 中文外挂自动加载探测(2026-09-19, 设计定稿即日实施): `ISubtitle::
      listSubtitleCandidates(videoUrl, SubtitleCandidate* out, cap)`(只增不改带默认实现),
      `SubtitleCandidate{path[512], SCodecId codec, lang[16], gbkHint}` 纯 POD。
      实现 `subtitle/SubtitleScan.cpp`(std::filesystem, C++17/20 char8_t 双兼容):
      同目录「同主名」扫描, 主名去扩展名, 候选=去扩展名后等于主名或「主名+分隔符
      (.-_)开头」(movies.srt 不命中 movie.mkv), 扩展名 .srt/.ass/.ssa(.ssa 归 ass);
      标记段: 首个语言段(zh/chs/cht/gb/big5/eng/zhhans/zhhant/zhcn/zhtw/jpn/jp/kor/kr)
      + .gbk 编码后缀进 hint 不参与匹配; 排序 完全同名 > 带标记, srt > ass, 同级按
      路径; 远程路径(带 ://)与缺目录返回 0, 参数非法返回负。纯查询不加载。
      单测 test_subcandidates(排序/边界/中文路径)。**avox-test 用例需求**:
      `sub-cand-basic`(movie.mkv + 同名/语言/gbk 素材, 断言排序与 hint)、
      `sub-cand-remote`(smb:// 返回 0)、`sub-cand-cjk`(中文目录+中文文件名可加载,
      吃 openFileUtf8 路径自愈 —— Windows ACP 无关, 素材可直接用中文名)。
      API 形状(2026-09-19 夜定稿, 即日落地, 未另走审批):
      `virtual int32_t listSubtitleCandidates(const char* videoUrl,
      SubtitleCandidate* out, int32_t cap) { return 0; }`于 ISubtitle 追加;
      返回值=实得总数(可 >cap, 截断填充), 负=参数错误。

## 验收

- avox-test playmatrix G 组全绿 + 像素取证(暗底亮字判据)。
- GBK/BOM 文件加载后字幕可读,且探测信号经 FFI 可见(panvox P-5 字幕体验包吃这个接口)。

## 风险与开放问题

- libass 真渲染绑定 `AVOX_ASS_DEPS_DIR` 产物,验收环境需先跑 `script/ass/build_windows.py` 固定产物。
- 时序竞态回归点:选轨时 extradata 未就绪窗口(`MediaPlayer.cpp:627`)与 pendingSubs 回放(:565)。
- 样式/信号接口动 `ISubtitle` ABI,SWIG 四语言(C#/Java/Node/python)同步是隐性工作量。
