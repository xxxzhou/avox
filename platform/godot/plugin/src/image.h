#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>

#include <avox/AvoxVideo.h>

namespace godot {

/// avox::IImageBuffer 封装 (RefCounted)。连续像素内存 (rgba8/bgra8/r8/yuv...),
/// 是图像操作与视觉识别的基本数据单元。对应 swig/python/avox/image.py。
///
/// 用法:
///   var img = AvoxImage.new()          # 自拥有空 buffer
///   img.load("x.png")                 # 从文件加载
///   var sub = img.crop(0,0,32,32)     # 返回新 AvoxImage
///   var gi: Image = img.to_image()    # 转 Godot Image (RGBA8)
///   img2.from_image(some_image)       # 从 Godot Image 拷入
///
/// 所有权: load/new/resize/crop/to_gray 产出的为 owned(析构 delete);
/// from_native_borrowed 包装的为借用(ScreenCapture.getBuffer 返回的归 capture 管)。
/// delete 路径与 python %newobject 一致 (virtual ~IImageBuffer)。
class AvoxImage : public RefCounted {
    GDCLASS(AvoxImage, RefCounted)

public:
    AvoxImage();
    ~AvoxImage();

    // ── 加载/保存 ──
    bool load(const String &p_path);        // 绝对路径 (BMP/PNG/JPG/TGA)
    bool load_asset(const String &p_name);  // assets/images/<name>
    bool save(const String &p_path);        // 按后缀选格式, 默认 PNG

    // ── Godot Image 互转 ──
    void from_image(const Ref<Image> &p_image);  // 拷入 (按源格式, 非 RGBA8 先 convert)
    Ref<Image> to_image() const;                  // → Godot Image (一律 RGBA8, 必要时 swap R/B)

    // ── 原始字节 ──
    PackedByteArray to_bytes() const;             // bufferSize() 字节
    bool from_bytes(const PackedByteArray &p_data);  // 需先 set_format, len 须 == buffer_size

    // ── 图像算子 (返回新 AvoxImage, 失败 null) ──
    Ref<AvoxImage> resize(int64_t p_w, int64_t p_h) const;
    Ref<AvoxImage> crop(int64_t p_x, int64_t p_y, int64_t p_w, int64_t p_h) const;
    Ref<AvoxImage> to_gray() const;  // r8 单通道灰度

    // ── 编码 ──
    // encode_type: IEncodeType (-1=默认jpg, 0=jpg 1=png 2=bmp 3=tga); quality 0~100
    String to_base64(int64_t p_encode_type, int64_t p_quality) const;

    // ── 像素统计 ──
    // roi=[x,y,w,h] 或 [] 整图; lo/hi=[c0,c1,c2] 或标量(广播三通道)
    // 返回 ROI 内 "所有通道 ∈ [lo,hi]" 像素占比 [0,1] (Variant: 可传 Array 或 int)
    float count_in_range(const Variant &p_roi, const Variant &p_lo, const Variant &p_hi) const;

    // ── 格式/尺寸 (只读) ──
    int64_t get_width() const;
    int64_t get_height() const;
    int64_t get_image_type() const;  // avox::ImageType
    int64_t get_buffer_size() const;
    bool is_valid() const;           // buffer 非空且格式有效

    // ── 高级: 手设格式 (配 from_bytes) ──
    // image_type 见 ImageType (rgba8=4 bgra8=5 r8=0 ...); row_pitch=0 默认 width*pixelSize
    bool set_format(int64_t p_w, int64_t p_h, int64_t p_image_type, int64_t p_row_pitch);

    // ── 内部: 供视觉检测器/ScreenCapture 取裸 avox buffer ──
    avox::IImageBuffer *get_native() const { return buffer; }

    // 包装借用 buffer (生命周期归外部, 不 delete)
    static Ref<AvoxImage> from_native_borrowed(avox::IImageBuffer *p_buf);
    // 收养 owning buffer (析构 delete)
    static Ref<AvoxImage> from_native_owned(avox::IImageBuffer *p_buf);

protected:
    static void _bind_methods();

private:
    avox::IImageBuffer *buffer = nullptr;
    bool owned = false;

    void release();          // delete owned buffer (借用只置空)
    void reset_owned();      // release + 新建空 owned buffer (load/from_image/set_format 用)
};

} // namespace godot
