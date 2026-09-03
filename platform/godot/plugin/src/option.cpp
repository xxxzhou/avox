#include "option.h"

#include <godot_cpp/core/class_db.hpp>

#include <avox/module/OptionKey.hpp>

#include <vector>

namespace godot {

// ── KEY 注册表 (key, ArgType, 常量名, 说明) ──
// 对应 python common._KEY_REGISTRY; get/set/as_dict 按此自动分发类型。
struct KeyEntry {
    const char *key;
    avox::ArgType type;
    const char *constName;
};
static const KeyEntry kKeyRegistry[] = {
    {AVOX_MP_LOW_LATENCY_BOOL, avox::ArgType::Boolean, "KEY_LOW_LATENCY"},
    {AVOX_MP_LL_SPEED_DOUBLE, avox::ArgType::Number, "KEY_LL_SPEED"},
    {AVOX_MP_DELAY_MS_INT, avox::ArgType::Int, "KEY_DELAY_MS"},
    {AVOX_MP_WINDOW_REFRESH_BOOL, avox::ArgType::Boolean, "KEY_WINDOW_DOUBLE_REFRESH"},
    {AVOX_MP_SPEED_TYPE_INT, avox::ArgType::Int, "KEY_SPEED_TYPE"},
    {AVOX_MP_IFRAME_ONLY_GT4_BOOL, avox::ArgType::Boolean, "KEY_IFRAME_ONLY_GT4"},
    {AVOX_MP_SYNC_TYPE_INT, avox::ArgType::Int, "KEY_SYNC_TYPE"},
    {AVOX_MP_IO_TIMEOUT_MS_INT, avox::ArgType::Int, "KEY_IO_TIMEOUT_MS"},
    {AVOX_MP_IO_RTSP_TRANSPORT_STR, avox::ArgType::String, "KEY_IO_RTSP_TRANSPORT"},
    {AVOX_MP_IO_TRACK_READY_MS_INT, avox::ArgType::Int, "KEY_IO_TRACK_READY_MS"},
    {AVOX_LOG_SOURCE_INPACKET_BOOL, avox::ArgType::Boolean, "KEY_LOG_SOURCE_PACKET"},
    {AVOX_LOG_DECODER_FRAME_BOOL, avox::ArgType::Boolean, "KEY_LOG_DECODER_FRAME"},
    {AVOX_LOG_RENDER_FRAME_BOOL, avox::ArgType::Boolean, "KEY_LOG_RENDER_FRAME"},
};
static constexpr int kKeyRegistryCount = sizeof(kKeyRegistry) / sizeof(kKeyRegistry[0]);

// 在注册表里查 key; 找到返回指针, 否则 nullptr
static const KeyEntry *findKeyEntry(const char *p_key) {
    for (int i = 0; i < kKeyRegistryCount; ++i) {
        if (strcmp(kKeyRegistry[i].key, p_key) == 0) {
            return &kKeyRegistry[i];
        }
    }
    return nullptr;
}

// ── 构造/析构 ──

AvoxOption::AvoxOption() {
    // .new() → 自拥有独立 JsonOption (与 python createJsonOption 对齐)
    option = avox::createJsonOption();
    owned = true;
}

AvoxOption::~AvoxOption() {
    release();
}

void AvoxOption::release() {
    if (owned && option) {
        delete option;
    }
    option = nullptr;
    owned = false;
}

// ── 类型化 set ──

void AvoxOption::set_bool(const String &p_key, bool p_value) {
    if (option) option->setBool(p_key.utf8().get_data(), p_value);
}

void AvoxOption::set_int(const String &p_key, int64_t p_value) {
    if (option) option->setInt(p_key.utf8().get_data(), p_value);
}

void AvoxOption::set_number(const String &p_key, double p_value) {
    if (option) option->setNumber(p_key.utf8().get_data(), p_value);
}

void AvoxOption::set_string(const String &p_key, const String &p_value) {
    if (option) option->setString(p_key.utf8().get_data(), p_value.utf8().get_data());
}

// ── 类型化 get ──

int AvoxOption::get_type(const String &p_key) const {
    if (!option) return (int)avox::ArgType::Null;
    return (int)option->getType(p_key.utf8().get_data());
}

bool AvoxOption::get_bool(const String &p_key) const {
    if (!option) return false;
    return option->getBool(p_key.utf8().get_data());
}

int64_t AvoxOption::get_int(const String &p_key) const {
    if (!option) return 0;
    return option->getInt(p_key.utf8().get_data());
}

double AvoxOption::get_double(const String &p_key) const {
    if (!option) return 0.0;
    return option->getDouble(p_key.utf8().get_data());
}

String AvoxOption::get_string(const String &p_key) const {
    if (!option) return String();
    // getString 返回内部缓冲, 立即深拷
    const char *s = option->getString(p_key.utf8().get_data());
    if (!s) return String();
    return String::utf8(s);
}

// ── 通用 get/set ──

Variant AvoxOption::get(const String &p_key, const Variant &p_default) const {
    if (!option) return p_default;
    std::string key = p_key.utf8().get_data();
    avox::ArgType argType = avox::ArgType::Null;
    if (const KeyEntry *e = findKeyEntry(key.c_str())) {
        argType = e->type;
    } else {
        argType = option->getType(key.c_str());
    }
    switch (argType) {
    case avox::ArgType::Null:
        return p_default;
    case avox::ArgType::Boolean:
        return option->getBool(key.c_str());
    case avox::ArgType::Int:
        return (int64_t)option->getInt(key.c_str());
    case avox::ArgType::Number:
        return option->getDouble(key.c_str());
    case avox::ArgType::String: {
        const char *s = option->getString(key.c_str());
        return s ? String::utf8(s) : String();
    }
    default:
        return p_default;
    }
}

void AvoxOption::set(const String &p_key, const Variant &p_value) {
    if (!option) return;
    std::string key = p_key.utf8().get_data();
    avox::ArgType argType = avox::ArgType::Null;
    if (const KeyEntry *e = findKeyEntry(key.c_str())) {
        argType = e->type;
    } else {
        // 未注册: 按 Variant 类型推断 (注意 bool 先于 int)
        switch (p_value.get_type()) {
        case Variant::BOOL: argType = avox::ArgType::Boolean; break;
        case Variant::INT: argType = avox::ArgType::Int; break;
        case Variant::FLOAT: argType = avox::ArgType::Number; break;
        case Variant::STRING: argType = avox::ArgType::String; break;
        default: argType = avox::ArgType::Null; break;
        }
    }
    switch (argType) {
    case avox::ArgType::Boolean:
        option->setBool(key.c_str(), (bool)p_value);
        break;
    case avox::ArgType::Int:
        option->setInt(key.c_str(), (int64_t)p_value);
        break;
    case avox::ArgType::Number:
        option->setNumber(key.c_str(), (double)p_value);
        break;
    case avox::ArgType::String: {
        String vs = (String)p_value;
        option->setString(key.c_str(), vs.utf8().get_data());
        break;
    }
    default:
        break;  // 不支持的类型, 忽略 (与 python raise 不同; Godot 侧静默)
    }
}

// ── 导出 ──

Dictionary AvoxOption::as_dict() const {
    Dictionary out;
    if (!option) return out;
    for (int i = 0; i < kKeyRegistryCount; ++i) {
        const KeyEntry &e = kKeyRegistry[i];
        if (option->getType(e.key) == avox::ArgType::Null) continue;
        switch (e.type) {
        case avox::ArgType::Boolean:
            out[String(e.key)] = option->getBool(e.key);
            break;
        case avox::ArgType::Int:
            out[String(e.key)] = (int64_t)option->getInt(e.key);
            break;
        case avox::ArgType::Number:
            out[String(e.key)] = option->getDouble(e.key);
            break;
        case avox::ArgType::String: {
            const char *s = option->getString(e.key);
            out[String(e.key)] = s ? String::utf8(s) : String();
            break;
        }
        default:
            break;
        }
    }
    return out;
}

Dictionary AvoxOption::keys() {
    Dictionary out;
    for (int i = 0; i < kKeyRegistryCount; ++i) {
        out[String(kKeyRegistry[i].constName)] = String(kKeyRegistry[i].key);
    }
    return out;
}

bool AvoxOption::is_valid() const {
    return option != nullptr;
}

// ── 内部 native 互操作 ──

Ref<AvoxOption> AvoxOption::from_native_borrowed(avox::IOption *p_opt) {
    Ref<AvoxOption> opt = memnew(AvoxOption);
    // memnew 会 new 一个 JsonOption; 借用场景丢弃它, 改挂外部 (不 delete 外部)
    if (opt->owned && opt->option) {
        delete opt->option;
    }
    opt->option = p_opt;
    opt->owned = false;
    return opt;
}

void AvoxOption::_bind_methods() {
    // 类型化 set
    ClassDB::bind_method(D_METHOD("set_bool", "key", "value"), &AvoxOption::set_bool);
    ClassDB::bind_method(D_METHOD("set_int", "key", "value"), &AvoxOption::set_int);
    ClassDB::bind_method(D_METHOD("set_number", "key", "value"), &AvoxOption::set_number);
    ClassDB::bind_method(D_METHOD("set_string", "key", "value"), &AvoxOption::set_string);
    // 类型化 get
    ClassDB::bind_method(D_METHOD("get_type", "key"), &AvoxOption::get_type);
    ClassDB::bind_method(D_METHOD("get_bool", "key"), &AvoxOption::get_bool);
    ClassDB::bind_method(D_METHOD("get_int", "key"), &AvoxOption::get_int);
    ClassDB::bind_method(D_METHOD("get_double", "key"), &AvoxOption::get_double);
    ClassDB::bind_method(D_METHOD("get_string", "key"), &AvoxOption::get_string);
    // 通用 get/set
    ClassDB::bind_method(D_METHOD("get", "key", "default"), &AvoxOption::get, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("set", "key", "value"), &AvoxOption::set);
    // 导出
    ClassDB::bind_method(D_METHOD("as_dict"), &AvoxOption::as_dict);
    ClassDB::bind_static_method("AvoxOption", D_METHOD("keys"), &AvoxOption::keys);
    ClassDB::bind_method(D_METHOD("is_valid"), &AvoxOption::is_valid);
}

} // namespace godot
