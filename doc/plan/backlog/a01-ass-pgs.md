# A-1 ASS/PGS 链收尾

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T1/T2/T3延迟/T4 PGS e2e/T5候选枚举/ass路径自愈 完成; 剩 ASS 轨样式缩放/字体覆盖) · 来源:backlog A-1
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
- [x] T4 PGS 真实样片 e2e(2026-09-19 全链绿, avox `2d4c6ac` + avox-test `48002b8/d0c3461/747135f`):
      **配方落地**: 无需真实样片, 纯 Python 合成 —— avox-test `assets/gen/gen_subtitle.py`
      (make_pgs_sup/_seg/_pcs/_wds/_pds/_ods/_rle_rect), sup 段格式核 pgssubdec.c
      (PCS 头含 composition_number/palette_id; ODS 长度字段含宽高 4 字节; RLE 用
      flags 位编码)。ffmpeg `-c copy` 封装 sub_pgs_embed.mkv(注意: mkv 首字幕包
      重定基到 0, 事件轴整体提前)。用例 `sub-pgs-e2e`(时窗亮暗取证) +
      `sub-pgs-seek`(seek 续显) + `sub-cand-basic/cjk` 离线 51/0/24 全绿,
      e2e 压测 ×3 稳定。**引擎侧三处 bug 修复**:
      1. sIndexMaps 时序: parseStream 时映射未填, 等值门控恒假 → 解码器建立改按
         codec 判, 喂包门控改 pgsStreamId 流索引(`IOParseFF.cpp`);
      2. 单槽画布: 旁路包全速先到, setPgsCanvas 只留最新 → 清屏态覆盖显示态;
         改 PgsFrame deque 按 pts 排队, 渲染按播放位置取帧(`SubtitleView.cpp`);
      3. onPgsFrame 的 trackOpened 门槛: 选轨命令与首包赛跑时帧被丢 → 去门槛,
         帧照常入缓冲(视图槽位仲裁兜底)(`MediaPlayer.cpp`)。
      ⚠️ **构建教训**: FFmpeg 白名单补 pgssub 重编 dll 后, 必须用
      `script/ffmpeg/make_msvc_lib.py` 重生成 MSVC 导入库 —— lib.exe /def 产物含
      按序号导入记录, dll 导出位移会让 avox.dll 绑错函数(段错误在 avsubtitle_free)。
- [x] T5 中文外挂自动加载探测(2026-09-19, 设计定稿即日实施): `ISubtitle::
      listSubtitleCandidates(videoUrl)`(扫描排序后引擎缓存, 返回个数, 带默认实现) +
      `ISubtitle::getSubtitleCandidate(index)`(取缓存项, 越界 nullptr), 候选项为
      `ISubtitleCandidate` 只读接口(getPath/getCodec/getLang/getGbkHint; 托管对象,
      生命周期至下次扫描/换源/close, 约定同 ISTrackDesc)。
      **API 形状返工(2026-09-19)**: 初版为出参数组
      `listSubtitleCandidates(videoUrl, SubtitleCandidate* out, cap)` +
      `SubtitleCandidate{path[512], codec, lang[16], gbkHint}` 纯 POD —— SWIG 把
      `SubtitleCandidate*` 包成**单对象指针**, C# 传一个对象引擎写 cap 份即堆破坏,
      出参数组形态绑不过去; 改接口 + 扫描缓存形态(仓内惯例: `get*` 返回托管对象,
      const char* 经绑定层当场拷贝, 生命周期规则见 AvoxPlayer.h)。
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
      API 形状(2026-09-19 夜定稿, 即日落地; 同日按上段返工为接口形态):
      `virtual int32_t listSubtitleCandidates(const char* videoUrl) { return 0; }`
      与 `virtual ISubtitleCandidate* getSubtitleCandidate(int32_t index) { return nullptr; }`
      于 ISubtitle 追加(带默认实现, 既有实现者零影响)。

## 验收

- avox-test playmatrix G 组全绿 + 像素取证(暗底亮字判据)。
- GBK/BOM 文件加载后字幕可读,且探测信号经 FFI 可见(panvox P-5 字幕体验包吃这个接口)。

## 风险与开放问题

- libass 真渲染绑定 `AVOX_ASS_DEPS_DIR` 产物,验收环境需先跑 `script/ass/build_windows.py` 固定产物。
- 时序竞态回归点:选轨时 extradata 未就绪窗口(`MediaPlayer.cpp:627`)与 pendingSubs 回放(:565)。
- 样式/信号接口动 `ISubtitle` ABI,SWIG 四语言(C#/Java/Node/python)同步是隐性工作量。
