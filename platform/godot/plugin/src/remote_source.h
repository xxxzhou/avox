#pragma once

#include <atomic>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <avox/AvoxBase.h>

#include "option.h"

namespace godot {

// 回调桥(定义在 remote_source.cpp, 命名空间级, 同 PlayerOb 模式)
class RemoteOb;

/// avox::IRemoteSource 封装 (RefCounted)。远程内容源统一会话:
/// 磁力/WebDAV/SMB/Alist/网盘/媒体服务器。模型: open(入口)建会话 → list(token)
/// 列子项(目录 token 下钻) → resolve(条目)产出可播放 URL。
/// 实现: avox_torrent 插件 ("torrent"); 插件未装时 open() 返回 false。
///
/// 用法 (磁力):
///   var src = RemoteSource.new()
///   src.open_result.connect(func(code): if code == 0: src.list(""))
///   src.list_result.connect(func(code): var entries = src.get_entries() ...)
///   src.open("magnet:?xt=urn:btih:...", 45000)   # 异步, 立即返回
///   # 选中条目后:
///   var opt = AvoxOption.new()
///   var play_url = src.resolve(idx, opt)   # 磁力即原链, torrent.fileIndex 写入 opt
///   player.play(play_url)
class RemoteSource : public RefCounted {
    GDCLASS(RemoteSource, RefCounted)

public:
    RemoteSource();
    ~RemoteSource();

    // 会话参数 (open 前设置; 实现自定义键: torrent 用 "cacheDir"/"extraTrackers")
    void set_param(const String &p_key, const String &p_value);
    // 异步建会话 (avox 工作线程, 完成发 open_result 信号; 本调用立即返回)
    bool open(const String &p_url, int p_timeout_ms);
    // 异步列节点子项 (token 空 = 根; 完成发 list_result 信号)
    bool list(const String &p_node_token);
    // 关闭会话并打断进行中的操作
    void close();
    bool is_opened();
    // open/list 进行中 (调用成功后到回调到达前为 true, 供 UI 防重入)
    bool is_busy() const;

    // ---- 结果读取 (list_result(0) 后有效) ----
    int get_entry_count() const;
    // 单个条目: {type(0=dir,1=media,2=file), name, token, size, media}
    Dictionary get_entry(int p_i) const;
    // 全部条目 (目录在前, 名称升序)
    Array get_entries() const;
    // 会话级信息 (torrent: "name"/"infoHash"/"totalSize")
    String get_session_field(const String &p_key) const;
    String get_last_error() const;

    // ---- 选择播放 ----
    // 条目 → 可播放 URL (磁力返回原链并把 torrent.fileIndex 写入 p_option);
    // 不可播返回空串, 原因见 get_last_error
    String resolve(int p_index, Ref<AvoxOption> p_option);

protected:
    static void _bind_methods();

private:
    avox::IRemoteSource *src = nullptr;

    // 回调桥: avox 工作线程 -> call_deferred 主线程信号
    friend class RemoteOb;
    RemoteOb *ob = nullptr;
    // busy 标记: 发起点置位, 工作线程回调清除(atomic, 跨线程)
    std::atomic<bool> busy{false};
};

} // namespace godot
