#include "remote_source.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// ── avox 会话观察者 (工作线程回调 -> busy 清除 + call_deferred 主线程信号) ──
class RemoteOb : public avox::IRemoteSourceOb {
public:
    RemoteSource *owner = nullptr;

    void onOpenResult(int32_t code) override {
        if (owner) {
            owner->busy.store(false, std::memory_order_release);
            owner->call_deferred("emit_signal", "open_result", static_cast<int>(code));
        }
    }
    void onListResult(int32_t code) override {
        if (owner) {
            owner->busy.store(false, std::memory_order_release);
            owner->call_deferred("emit_signal", "list_result", static_cast<int>(code));
        }
    }
};

RemoteSource::RemoteSource() {
    src = avox::createRemoteSource("torrent");
    if (src) {
        ob = new RemoteOb();
        ob->owner = this;
        src->setOb(ob);
    }
}

RemoteSource::~RemoteSource() {
    if (src) {
        src->close();
        delete src;
        src = nullptr;
    }
    delete ob;
    ob = nullptr;
}

void RemoteSource::set_param(const String &p_key, const String &p_value) {
    if (src) {
        src->setParam(p_key.utf8().get_data(), p_value.utf8().get_data());
    }
}

bool RemoteSource::open(const String &p_url, int p_timeout_ms) {
    if (!src) {
        ERR_PRINT("RemoteSource: avox_torrent plugin not loaded");
        return false;
    }
    bool ok = src->open(p_url.utf8().get_data(), nullptr, nullptr, nullptr,
                        static_cast<int32_t>(p_timeout_ms));
    busy.store(ok, std::memory_order_release);
    return ok;
}

bool RemoteSource::list(const String &p_node_token) {
    if (!src) {
        return false;
    }
    bool ok = src->list(p_node_token.utf8().get_data(), 0);
    busy.store(ok, std::memory_order_release);
    return ok;
}

void RemoteSource::close() {
    if (src) {
        src->close();
    }
    busy.store(false, std::memory_order_release);
}

bool RemoteSource::is_opened() {
    return src ? src->opened() : false;
}

bool RemoteSource::is_busy() const {
    return busy.load(std::memory_order_acquire);
}

int RemoteSource::get_entry_count() const {
    // IRemoteSource getter 为非 const (内部锁), 此处去 const 包装
    return src ? const_cast<avox::IRemoteSource *>(src)->getEntryCount() : 0;
}

Dictionary RemoteSource::get_entry(int p_i) const {
    Dictionary d;
    if (!src) return d;
    auto *s = const_cast<avox::IRemoteSource *>(src);
    if (p_i < 0 || p_i >= s->getEntryCount()) return d;
    auto type = s->getEntryType(p_i);
    d["type"] = static_cast<int>(type);
    d["name"] = String(s->getEntryName(p_i));
    d["token"] = String(s->getEntryToken(p_i));
    d["size"] = static_cast<int64_t>(s->getEntrySize(p_i));
    d["media"] = type == avox::RemoteEntryType::media;
    return d;
}

Array RemoteSource::get_entries() const {
    Array arr;
    if (!src) return arr;
    auto *s = const_cast<avox::IRemoteSource *>(src);
    int n = s->getEntryCount();
    for (int i = 0; i < n; ++i) {
        arr.push_back(get_entry(i));
    }
    return arr;
}

String RemoteSource::get_session_field(const String &p_key) const {
    if (!src) return String();
    auto *s = const_cast<avox::IRemoteSource *>(src);
    return String(s->getSessionField(p_key.utf8().get_data()));
}

String RemoteSource::get_last_error() const {
    if (!src) return String();
    auto *s = const_cast<avox::IRemoteSource *>(src);
    return String(s->getLastError());
}

String RemoteSource::resolve(int p_index, Ref<AvoxOption> p_option) {
    if (!src) return String();
    avox::IOption *native = (p_option.is_valid() && !p_option.is_null())
                                ? p_option->get_native() : nullptr;
    const char *u = src->resolve(static_cast<int32_t>(p_index), native);
    return String(u ? u : "");
}

void RemoteSource::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_param", "key", "value"), &RemoteSource::set_param);
    ClassDB::bind_method(D_METHOD("open", "url", "timeout_ms"),
                         &RemoteSource::open, DEFVAL(45000));
    ClassDB::bind_method(D_METHOD("list", "node_token"), &RemoteSource::list, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("close"), &RemoteSource::close);
    ClassDB::bind_method(D_METHOD("is_opened"), &RemoteSource::is_opened);
    ClassDB::bind_method(D_METHOD("is_busy"), &RemoteSource::is_busy);
    ClassDB::bind_method(D_METHOD("get_entry_count"), &RemoteSource::get_entry_count);
    ClassDB::bind_method(D_METHOD("get_entry", "i"), &RemoteSource::get_entry);
    ClassDB::bind_method(D_METHOD("get_entries"), &RemoteSource::get_entries);
    ClassDB::bind_method(D_METHOD("get_session_field", "key"), &RemoteSource::get_session_field);
    ClassDB::bind_method(D_METHOD("get_last_error"), &RemoteSource::get_last_error);
    ClassDB::bind_method(D_METHOD("resolve", "index", "option"), &RemoteSource::resolve);
    // 会话建立结束 (code=0 成功; 负数失败见 get_last_error) — 工作线程经 call_deferred 发出
    ADD_SIGNAL(MethodInfo("open_result", PropertyInfo(Variant::INT, "code")));
    // 一批结果就绪 (list/search 共用) — 工作线程经 call_deferred 发出
    ADD_SIGNAL(MethodInfo("list_result", PropertyInfo(Variant::INT, "code")));
}

} // namespace godot
