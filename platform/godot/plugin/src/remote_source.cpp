#include "source_probe.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// ── avox 探测观察者 (探测线程回调 -> call_deferred 主线程信号) ──
class ProbeOb : public avox::ISourceProbeOb {
public:
    SourceProbe *owner = nullptr;

    void onProbeResult(int32_t code) override {
        if (owner) {
            owner->call_deferred("emit_signal", "probe_result", static_cast<int>(code));
        }
    }
};

SourceProbe::SourceProbe() {
    probe = avox::createSourceProbe("torrent");
    if (probe) {
        ob = new ProbeOb();
        ob->owner = this;
        probe->setOb(ob);
    }
}

SourceProbe::~SourceProbe() {
    if (probe) {
        probe->stop();
        delete probe;
        probe = nullptr;
    }
    delete ob;
    ob = nullptr;
}

bool SourceProbe::start(const String &p_url, const String &p_cache_dir, int p_timeout_ms) {
    if (!probe) {
        ERR_PRINT("SourceProbe: avox_torrent plugin not loaded");
        return false;
    }
    return probe->start(p_url.utf8().get_data(), p_cache_dir.utf8().get_data(),
                        static_cast<int32_t>(p_timeout_ms));
}

void SourceProbe::stop() {
    if (probe) {
        probe->stop();
    }
}

bool SourceProbe::is_probing() {
    return probe ? probe->probing() : false;
}

int SourceProbe::get_file_count() const {
    // ISourceProbe getter 为非 const (内部锁), 此处去 const 包装
    return probe ? const_cast<avox::ISourceProbe *>(probe)->getFileCount() : 0;
}

Dictionary SourceProbe::get_file(int p_i) const {
    Dictionary d;
    if (!probe) return d;
    auto *p = const_cast<avox::ISourceProbe *>(probe);
    if (p_i < 0 || p_i >= p->getFileCount()) return d;
    d["index"] = p->getFileIndex(p_i);
    d["path"] = String(p->getFilePath(p_i));
    d["size"] = static_cast<int64_t>(p->getFileSize(p_i));
    d["media"] = p->isMediaFile(p_i);
    return d;
}

Array SourceProbe::get_files() const {
    Array arr;
    if (!probe) return arr;
    auto *p = const_cast<avox::ISourceProbe *>(probe);
    int n = p->getFileCount();
    // 视频扩展名排前, 各段内保持种子原序; index 仍为种子内原始索引
    Array media;
    Array others;
    for (int i = 0; i < n; ++i) {
        Dictionary d;
        d["index"] = p->getFileIndex(i);
        d["path"] = String(p->getFilePath(i));
        d["size"] = static_cast<int64_t>(p->getFileSize(i));
        d["media"] = p->isMediaFile(i);
        if (p->isMediaFile(i)) {
            media.push_back(d);
        } else {
            others.push_back(d);
        }
    }
    arr.append_array(media);
    arr.append_array(others);
    return arr;
}

String SourceProbe::get_name() const {
    return probe ? String(const_cast<avox::ISourceProbe *>(probe)->getName()) : String();
}

String SourceProbe::get_info_hash() const {
    return probe ? String(const_cast<avox::ISourceProbe *>(probe)->getInfoHash()) : String();
}

int64_t SourceProbe::get_total_size() const {
    return probe ? static_cast<int64_t>(const_cast<avox::ISourceProbe *>(probe)->getTotalSize()) : 0;
}

String SourceProbe::get_last_error() const {
    return probe ? String(const_cast<avox::ISourceProbe *>(probe)->getLastError()) : String();
}

void SourceProbe::select_file(int p_index) {
    if (probe) {
        probe->selectFile(static_cast<int32_t>(p_index));
    }
}

int SourceProbe::get_selected_index() const {
    return probe ? const_cast<avox::ISourceProbe *>(probe)->getSelectedIndex() : -1;
}

bool SourceProbe::apply_to_option(Ref<AvoxOption> p_option) {
    if (!probe || p_option.is_null() || p_option->get_native() == nullptr) {
        return false;
    }
    return probe->applyToOption(p_option->get_native());
}

void SourceProbe::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start", "url", "cache_dir", "timeout_ms"),
                         &SourceProbe::start, DEFVAL(""), DEFVAL(45000));
    ClassDB::bind_method(D_METHOD("stop"), &SourceProbe::stop);
    ClassDB::bind_method(D_METHOD("is_probing"), &SourceProbe::is_probing);
    ClassDB::bind_method(D_METHOD("get_file_count"), &SourceProbe::get_file_count);
    ClassDB::bind_method(D_METHOD("get_file", "i"), &SourceProbe::get_file);
    ClassDB::bind_method(D_METHOD("get_files"), &SourceProbe::get_files);
    ClassDB::bind_method(D_METHOD("get_name"), &SourceProbe::get_name);
    ClassDB::bind_method(D_METHOD("get_info_hash"), &SourceProbe::get_info_hash);
    ClassDB::bind_method(D_METHOD("get_total_size"), &SourceProbe::get_total_size);
    ClassDB::bind_method(D_METHOD("get_last_error"), &SourceProbe::get_last_error);
    ClassDB::bind_method(D_METHOD("select_file", "index"), &SourceProbe::select_file);
    ClassDB::bind_method(D_METHOD("get_selected_index"), &SourceProbe::get_selected_index);
    ClassDB::bind_method(D_METHOD("apply_to_option", "option"), &SourceProbe::apply_to_option);
    // 探测结束 (code=0 成功; 非 0 见 get_last_error) — 探测线程经 call_deferred 发出
    ADD_SIGNAL(MethodInfo("probe_result", PropertyInfo(Variant::INT, "code")));
}

} // namespace godot
