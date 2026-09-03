#include "image.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <algorithm>

namespace godot {

// 把 Variant (int 或 [c0(,c1(,c2))]) 规范化为 3 通道 int (不足按末值广播)
static void normalizeRange3(const Variant &p_v, int32_t p_out[3]) {
    if (p_v.get_type() == Variant::ARRAY) {
        Array a = (Array)p_v;
        int n = a.size();
        if (n <= 0) {
            p_out[0] = p_out[1] = p_out[2] = 0;
            return;
        }
        p_out[0] = (int32_t)(int64_t)a[0];
        p_out[1] = (n >= 2) ? (int32_t)(int64_t)a[1] : p_out[0];
        p_out[2] = (n >= 3) ? (int32_t)(int64_t)a[2] : p_out[1];
    } else {
        int32_t iv = (int32_t)(int64_t)p_v;
        p_out[0] = p_out[1] = p_out[2] = iv;
    }
}

AvoxImage::AvoxImage() {}

AvoxImage::~AvoxImage() {
    release();
}

void AvoxImage::release() {
    // delete 路径与 python %newobject 一致 (IImageBuffer 有 virtual ~)
    if (owned && buffer) {
        delete buffer;
    }
    buffer = nullptr;
    owned = false;
}

void AvoxImage::reset_owned() {
    release();
    buffer = avox::createImageBuffer();
    owned = true;
}

// ── 加载/保存 ──

bool AvoxImage::load(const String &p_path) {
    if (p_path.is_empty()) return false;
    reset_owned();
    return avox::loadImagePath(p_path.utf8().get_data(), buffer);
}

bool AvoxImage::load_asset(const String &p_name) {
    if (p_name.is_empty()) return false;
    reset_owned();
    return avox::loadImageAsset(p_name.utf8().get_data(), buffer);
}

bool AvoxImage::save(const String &p_path) {
    if (!buffer || p_path.is_empty()) return false;
    return avox::saveImagePath(p_path.utf8().get_data(), buffer);
}

// ── Godot Image 互转 ──

void AvoxImage::from_image(const Ref<Image> &p_image) {
    if (!p_image.is_valid() || p_image->is_empty()) {
        return;
    }
    // Godot Image 格式不一, 先规整到 RGBA8 (duplicate 不污染调用方)
    Ref<Image> src = p_image;
    if (src->get_format() != Image::FORMAT_RGBA8) {
        src = src->duplicate();
        if (src.is_valid()) {
            src->convert(Image::FORMAT_RGBA8);
        }
    }
    if (!src.is_valid()) return;
    int w = src->get_width();
    int h = src->get_height();
    if (w <= 0 || h <= 0) return;
    reset_owned();
    avox::ImageFormat fmt;
    fmt.width = w;
    fmt.height = h;
    fmt.rowPitch = 0;  // = width * pixelSize (4)
    fmt.imageType = avox::ImageType::rgba8;
    buffer->setImageFormat(fmt);
    int32_t bs = buffer->getBufferSize();
    if (bs <= 0) return;
    PackedByteArray data = src->get_data();
    // RGBA8 level-0 在 get_data() 起始处连续 w*h*4; 有 mipmap 也只拷 level0
    int64_t copyBytes = std::min<int64_t>(bs, (int64_t)data.size());
    if (copyBytes > 0) {
        memcpy(buffer->getPointer(), data.ptr(), (size_t)copyBytes);
    }
}

Ref<Image> AvoxImage::to_image() const {
    if (!buffer) return Ref<Image>();
    avox::ImageFormat fmt = buffer->getImageFormat();
    if (!fmt.bVailid()) return Ref<Image>();
    int32_t w = fmt.width;
    int32_t h = fmt.height;
    const uint8_t *src = buffer->getPointer();
    int32_t bs = buffer->getBufferSize();
    if (!src || bs <= 0 || w <= 0 || h <= 0) return Ref<Image>();
    int32_t pixelSize = avox::getPixelSize(fmt.imageType);
    if (pixelSize <= 0) return Ref<Image>();
    int32_t pitch = (fmt.rowPitch > 0) ? fmt.rowPitch : (w * pixelSize);
    // Godot Image 一律输出 RGBA8 (R,G,B,A); 按 avox imageType 分派转换, 尊重 pitch
    PackedByteArray rgba;
    rgba.resize((int64_t)w * h * 4);
    uint8_t *out = rgba.ptrw();
    int64_t count = (int64_t)w * h;
    switch (fmt.imageType) {
    case avox::ImageType::rgba8:
        if (pitch == w * 4) {
            memcpy(out, src, (size_t)(count * 4));
        } else {
            for (int32_t y = 0; y < h; ++y) {
                memcpy(out + (int64_t)y * w * 4, src + (int64_t)y * pitch, (size_t)w * 4);
            }
        }
        break;
    case avox::ImageType::bgra8:
        // B,G,R,A → R,G,B,A (swap byte0/byte2)
        for (int32_t y = 0; y < h; ++y) {
            const uint8_t *row = src + (int64_t)y * pitch;
            uint8_t *dst = out + (int64_t)y * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 2];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4 + 0];
                dst[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        break;
    case avox::ImageType::argb8:
        // A,R,G,B → R,G,B,A
        for (int32_t y = 0; y < h; ++y) {
            const uint8_t *row = src + (int64_t)y * pitch;
            uint8_t *dst = out + (int64_t)y * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 1];
                dst[x * 4 + 1] = row[x * 4 + 2];
                dst[x * 4 + 2] = row[x * 4 + 3];
                dst[x * 4 + 3] = row[x * 4 + 0];
            }
        }
        break;
    case avox::ImageType::rgb8:
        // R,G,B → R,G,B,255
        for (int32_t y = 0; y < h; ++y) {
            const uint8_t *row = src + (int64_t)y * pitch;
            uint8_t *dst = out + (int64_t)y * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 3 + 0];
                dst[x * 4 + 1] = row[x * 3 + 1];
                dst[x * 4 + 2] = row[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
        break;
    case avox::ImageType::bgr8:
        // B,G,R → R,G,B,255
        for (int32_t y = 0; y < h; ++y) {
            const uint8_t *row = src + (int64_t)y * pitch;
            uint8_t *dst = out + (int64_t)y * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 3 + 2];
                dst[x * 4 + 1] = row[x * 3 + 1];
                dst[x * 4 + 2] = row[x * 3 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
        break;
    case avox::ImageType::r8:
        // gray → R=G=B=v, A=255
        for (int32_t y = 0; y < h; ++y) {
            const uint8_t *row = src + (int64_t)y * pitch;
            uint8_t *dst = out + (int64_t)y * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                uint8_t v = row[x];
                dst[x * 4 + 0] = v;
                dst[x * 4 + 1] = v;
                dst[x * 4 + 2] = v;
                dst[x * 4 + 3] = 255;
            }
        }
        break;
    default:
        // yuv/float/平面等不在 to_image 直转范围 (留给专用路径)
        return Ref<Image>();
    }
    return Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, rgba);
}

// ── 原始字节 ──

PackedByteArray AvoxImage::to_bytes() const {
    PackedByteArray out;
    if (!buffer) return out;
    int32_t bs = buffer->getBufferSize();
    if (bs <= 0) return out;
    out.resize(bs);
    memcpy(out.ptrw(), buffer->getPointer(), (size_t)bs);
    return out;
}

bool AvoxImage::from_bytes(const PackedByteArray &p_data) {
    if (!buffer) return false;
    int32_t bs = buffer->getBufferSize();
    if (bs <= 0 || p_data.size() < bs) return false;
    memcpy(buffer->getPointer(), p_data.ptr(), (size_t)bs);
    return true;
}

// ── 图像算子 ──

Ref<AvoxImage> AvoxImage::resize(int64_t p_w, int64_t p_h) const {
    if (!buffer || p_w <= 0 || p_h <= 0) return Ref<AvoxImage>();
    Ref<AvoxImage> out = memnew(AvoxImage);
    out->reset_owned();
    if (!avox::resizeImage(buffer, out->buffer, (int32_t)p_w, (int32_t)p_h)) {
        return Ref<AvoxImage>();
    }
    return out;
}

Ref<AvoxImage> AvoxImage::crop(int64_t p_x, int64_t p_y, int64_t p_w, int64_t p_h) const {
    if (!buffer || p_w <= 0 || p_h <= 0) return Ref<AvoxImage>();
    Ref<AvoxImage> out = memnew(AvoxImage);
    out->reset_owned();
    if (!avox::cropImage(buffer, out->buffer, (int32_t)p_x, (int32_t)p_y, (int32_t)p_w, (int32_t)p_h)) {
        return Ref<AvoxImage>();
    }
    return out;
}

Ref<AvoxImage> AvoxImage::to_gray() const {
    if (!buffer) return Ref<AvoxImage>();
    avox::IImageBuffer *g = avox::toGrayImage(buffer);
    if (!g) return Ref<AvoxImage>();
    Ref<AvoxImage> out = memnew(AvoxImage);
    out->buffer = g;
    out->owned = true;
    return out;
}

// ── 编码 ──

String AvoxImage::to_base64(int64_t p_encode_type, int64_t p_quality) const {
    if (!buffer) return String();
    avox::IEncodeConfig cfg;
    // -1 = 默认 jpg (与 python 一致)
    cfg.encodeType = (p_encode_type < 0) ? avox::IEncodeType::jpg : static_cast<avox::IEncodeType>(p_encode_type);
    cfg.quality = (int32_t)p_quality;
    const char *b64 = avox::getImageBase64(buffer, cfg);
    // 静态缓冲, 立即深拷 (见 AvoxVideo.h 注释)
    if (!b64) return String();
    return String::utf8(b64);
}

// ── 像素统计 ──

float AvoxImage::count_in_range(const Variant &p_roi, const Variant &p_lo, const Variant &p_hi) const {
    if (!buffer) return 0.0f;
    avox::ImageFormat fmt = buffer->getImageFormat();
    int32_t x, y, w, h;
    if (p_roi.get_type() == Variant::ARRAY) {
        Array a = (Array)p_roi;
        if (a.size() >= 4) {
            x = (int32_t)(int64_t)a[0];
            y = (int32_t)(int64_t)a[1];
            w = (int32_t)(int64_t)a[2];
            h = (int32_t)(int64_t)a[3];
        } else {
            x = 0;
            y = 0;
            w = fmt.width;
            h = fmt.height;
        }
    } else {
        x = 0;
        y = 0;
        w = fmt.width;
        h = fmt.height;
    }
    int32_t lo3[3], hi3[3];
    normalizeRange3(p_lo, lo3);
    normalizeRange3(p_hi, hi3);
    return avox::countInRange(buffer, x, y, w, h,
                             lo3[0], lo3[1], lo3[2], hi3[0], hi3[1], hi3[2]);
}

// ── 格式/尺寸 ──

int64_t AvoxImage::get_width() const {
    if (!buffer) return 0;
    return buffer->getImageFormat().width;
}

int64_t AvoxImage::get_height() const {
    if (!buffer) return 0;
    return buffer->getImageFormat().height;
}

int64_t AvoxImage::get_image_type() const {
    if (!buffer) return (int64_t)avox::ImageType::other;
    return (int64_t)buffer->getImageFormat().imageType;
}

int64_t AvoxImage::get_buffer_size() const {
    if (!buffer) return 0;
    return buffer->getBufferSize();
}

bool AvoxImage::is_valid() const {
    if (!buffer) return false;
    avox::ImageFormat fmt = buffer->getImageFormat();
    return fmt.bVailid() && buffer->getBufferSize() > 0;
}

bool AvoxImage::set_format(int64_t p_w, int64_t p_h, int64_t p_image_type, int64_t p_row_pitch) {
    if (p_w <= 0 || p_h <= 0) return false;
    reset_owned();
    avox::ImageFormat fmt;
    fmt.width = (int32_t)p_w;
    fmt.height = (int32_t)p_h;
    fmt.rowPitch = (int32_t)p_row_pitch;
    fmt.imageType = static_cast<avox::ImageType>(p_image_type);
    buffer->setImageFormat(fmt);
    return true;
}

// ── 内部 native 互操作 (C++-only, 不绑定 GDScript) ──

Ref<AvoxImage> AvoxImage::from_native_borrowed(avox::IImageBuffer *p_buf) {
    Ref<AvoxImage> img = memnew(AvoxImage);
    img->buffer = p_buf;
    img->owned = false;  // 生命周期归外部 (如 ScreenCapture.getBuffer)
    return img;
}

Ref<AvoxImage> AvoxImage::from_native_owned(avox::IImageBuffer *p_buf) {
    Ref<AvoxImage> img = memnew(AvoxImage);
    img->buffer = p_buf;
    img->owned = true;
    return img;
}

void AvoxImage::_bind_methods() {
    // 加载/保存
    ClassDB::bind_method(D_METHOD("load", "path"), &AvoxImage::load);
    ClassDB::bind_method(D_METHOD("load_asset", "name"), &AvoxImage::load_asset);
    ClassDB::bind_method(D_METHOD("save", "path"), &AvoxImage::save);
    // Godot Image 互转
    ClassDB::bind_method(D_METHOD("from_image", "image"), &AvoxImage::from_image);
    ClassDB::bind_method(D_METHOD("to_image"), &AvoxImage::to_image);
    // 原始字节
    ClassDB::bind_method(D_METHOD("to_bytes"), &AvoxImage::to_bytes);
    ClassDB::bind_method(D_METHOD("from_bytes", "data"), &AvoxImage::from_bytes);
    // 图像算子
    ClassDB::bind_method(D_METHOD("resize", "width", "height"), &AvoxImage::resize);
    ClassDB::bind_method(D_METHOD("crop", "x", "y", "width", "height"), &AvoxImage::crop);
    ClassDB::bind_method(D_METHOD("to_gray"), &AvoxImage::to_gray);
    // 编码
    ClassDB::bind_method(D_METHOD("to_base64", "encode_type", "quality"), &AvoxImage::to_base64, DEFVAL(-1), DEFVAL(85));
    // 像素统计
    ClassDB::bind_method(D_METHOD("count_in_range", "roi", "lo", "hi"), &AvoxImage::count_in_range);
    // 尺寸/格式 getter
    ClassDB::bind_method(D_METHOD("get_width"), &AvoxImage::get_width);
    ClassDB::bind_method(D_METHOD("get_height"), &AvoxImage::get_height);
    ClassDB::bind_method(D_METHOD("get_image_type"), &AvoxImage::get_image_type);
    ClassDB::bind_method(D_METHOD("get_buffer_size"), &AvoxImage::get_buffer_size);
    ClassDB::bind_method(D_METHOD("is_valid"), &AvoxImage::is_valid);
    ClassDB::bind_method(D_METHOD("set_format", "width", "height", "image_type", "row_pitch"), &AvoxImage::set_format, DEFVAL(0));
    // 只读属性 (空 setter = 不可写, 避免引用未绑定 setter 报错)
    ADD_PROPERTY(PropertyInfo(Variant::INT, "width"), "", "get_width");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "height"), "", "get_height");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "image_type"), "", "get_image_type");
}

} // namespace godot
