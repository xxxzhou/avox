#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <avox/AvoxBase.h>

namespace godot {

/// avox::IOption 封装 (RefCounted)。键值参数设置器, 含 KEY 常量注册表与自动类型分发。
/// 对应 swig/python/avox/common.py。
///
/// 用法:
///   var opt = AvoxOption.new()            # 自拥有独立 JsonOption
///   opt.set("mp.lowlatency", true)       # 通用 set (按注册表/Variant 类型自动分发)
///   opt.set_int("mp.delay.ms", 200)      # 类型确定时直接调
///   opt.get("mp.delay.ms")               # → 200 (按注册表类型取)
///   opt.get("unknown", 0)                # → 0 (不存在返回 default)
///   opt.as_dict()                        # {key: value, ...}
///   print(AvoxOption.keys())              # {CONST_NAME: "mp.lowlatency", ...}
///
/// 所有权: .new() 为 owned(析构 delete); MediaPlayer.get_option() 返回的为借用
/// (IOption 归 player 管) —— from_native_borrowed 包装, 不 delete。
class AvoxOption : public RefCounted {
    GDCLASS(AvoxOption, RefCounted)

public:
    AvoxOption();
    ~AvoxOption();

    // ── 类型化 set ──
    void set_bool(const String &p_key, bool p_value);
    void set_int(const String &p_key, int64_t p_value);
    void set_number(const String &p_key, double p_value);
    void set_string(const String &p_key, const String &p_value);

    // ── 类型化 get ──
    int get_type(const String &p_key) const;  // avox::ArgType (0=Null 1=bool 2=int 3=number 4=string)
    bool get_bool(const String &p_key) const;
    int64_t get_int(const String &p_key) const;
    double get_double(const String &p_key) const;
    String get_string(const String &p_key) const;

    // ── 通用 get/set (按 KEY 注册表或 Variant 类型自动分发) ──
    Variant get(const String &p_key, const Variant &p_default) const;
    void set(const String &p_key, const Variant &p_value);

    // ── 导出 ──
    Dictionary as_dict() const;  // {key: value} 遍历注册表且已设值的 key
    static Dictionary keys();    // {CONST_NAME: key_string} 全部 KEY 常量 (供发现/避免拼写错)

    bool is_valid() const;

    // ── 内部: 供 MediaPlayer.get_option() 包装借用 IOption ──
    avox::IOption *get_native() const { return option; }
    static Ref<AvoxOption> from_native_borrowed(avox::IOption *p_opt);

protected:
    static void _bind_methods();

private:
    avox::IOption *option = nullptr;
    bool owned = false;

    void release();
};

} // namespace godot
