#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <avox/AvoxBase.h>

#include "option.h"

namespace godot {

// 回调桥(定义在 source_probe.cpp, 命名空间级, 同 PlayerOb 模式)
class ProbeOb;

/// avox::ISourceProbe 封装 (RefCounted)。数据源文件列表探测 + 选文件。
/// 实现: avox_torrent 插件 ("torrent"); 插件未装时 start() 返回 false。
///
/// 用法:
///   var probe = SourceProbe.new()
///   probe.probe_result.connect(func(code): ...)   # code=0 成功
///   probe.start("magnet:?xt=urn:btih:...", "", 45000)   # 异步, 立即返回
///   var files = probe.get_files()   # [{index, path, size, media}, ...] 视频在前
///   probe.select_file(files[0]["index"])
///   probe.apply_to_option(player.get_option())      # 写 torrent.fileIndex
///   player.play(url)
class SourceProbe : public RefCounted {
    GDCLASS(SourceProbe, RefCounted)

public:
    SourceProbe();
    ~SourceProbe();

    // 异步探测 (avox 内部工作线程, 完成发 probe_result 信号; 本调用立即返回)
    // url: "magnet:?" 开头 或本地 .torrent 路径; cache_dir 与播放选项
    // torrent.cacheDir 传同值可让起播命中元数据缓存, 空用系统默认
    bool start(const String &p_url, const String &p_cache_dir, int p_timeout_ms);
    void stop();
    bool is_probing();

    // ---- 结果读取 (probe_result(0) 后有效) ----
    int get_file_count() const;
    // 单个文件: {index(种子内原始索引), path, size, media(视频扩展名)}
    Dictionary get_file(int p_i) const;
    // 全部文件, 视频扩展名排前(各段内保持种子原序)
    Array get_files() const;
    String get_name() const;
    String get_info_hash() const;
    int64_t get_total_size() const;
    String get_last_error() const;

    // ---- 文件选择 ----
    void select_file(int p_index);
    int get_selected_index() const;
    // 把选择写进播放选项 (torrent.fileIndex); p_option 传 player.get_option()
    bool apply_to_option(Ref<AvoxOption> p_option);

protected:
    static void _bind_methods();

private:
    avox::ISourceProbe *probe = nullptr;

    // 回调桥: avox 探测线程 -> call_deferred 主线程信号
    friend class ProbeOb;
    ProbeOb *ob = nullptr;
};

} // namespace godot
