> 整理自 aocec 仓库 `doc/virtualproduction/YUV10B与RGBA互转GPGPU.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# YUV10B与RGBA互转GPGPU实现

在现代硬件能力的加强下,出现视频的分辨率越来越大,颜色细节也更丰富,比如有屏幕宣传他家屏幕超10亿种颜色,我们知道RGB8下,一共256^3种组合只有16KW种颜色,如果要超10亿,需要提高每个通道更高BIT,如RGB10B下,就有超10亿种颜色.

如今也越来越多10bit视频,在编写相关Rivermax程序中,需要添加其CUDA对应的YUV10B与RGBA互相转化处理,记录下.

在这我们以YUV422交叉格式的10BIT转RGBA8做为例子,说明下如何使用CUDA来完成,以1080P为例,其YUV10BIT下,每二个像素二个Y,共用一个UV,一共四个元素,也就是4x10BIT,一共40BIT,对应五个8BIT,也就是说,如果把YUV10BIT的422交叉格式图像当成R(8BIT)单通道来看的话,其宽度应该为1920*5/2Byte,高度不变,对应一张4800x1080的R单通道8BIT图.

在处理这种所有数据相互间无关系,GPU最容易处理了,首先划分组来处理,分组在这种数据间无关系情况下,只需要注意多个核心不写同样显存位置就好了.

``` c++
void yuv2rgb_gpu(PtrStepSz<uchar> source, PtrStepSz<uchar4> dest,
                 int32_t yuvtype, cudaStream_t stream) {
    dim3 grid(divUp(dest.width / 2, block.x), divUp(dest.height, block.y));
    yuv10b2rgb<<<grid, block, 0, stream>>>(source, dest);
}
```

说明下代码,以1080P为例,输入的source是4800x1080的R8图像,结果是1920*1080的RGBA8图像,其分成(1920/2/16=60,1080/16=68)共4080个块进行处理.

``` c++
// 10bitYUV422
inline __global__ void yuv10b2rgb(PtrStepSz<uchar> source,
                                  PtrStepSz<uchar4> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  const uint16_t mask10bit = 0x03FF;
  // 一次处理二个像素,对应source中的5个byte
  if (idx < dest.width / 2 && idy < source.height) {
    // 对于小于uint32类型,左移都会提升到uint32
    // https://www.coder.work/article/7301177
    uint16_t u =
        ((source(idy, idx * 5) << 2) | (source(idy, idx * 5 + 1) >> 6)) &
        mask10bit;
    uint16_t y1 =
        ((source(idy, idx * 5 + 1) << 4) | (source(idy, idx * 5 + 2) >> 4)) &
        mask10bit;
    uint16_t v =
        ((source(idy, idx * 5 + 2) << 6) | (source(idy, idx * 5 + 3) >> 2)) &
        mask10bit;
    uint16_t y2 =
        ((source(idy, idx * 5 + 3) << 8) | (source(idy, idx * 5 + 4))) &
        mask10bit;
    ushort4 suyuv = make_ushort4(u, y1, v, y2);
    float4 yuyv = uint102float4(suyuv);
    float3 yuv = make_float3(yuyv.y, yuyv.x, yuyv.z);
    dest(idy, idx * 2) = rgbafloat42uchar4(make_float4(yuv2Rgb(yuv), 1.f));
    float3 yuv1 = make_float3(yuyv.w, yuyv.x, yuyv.z);
    dest(idy, idx * 2 + 1) = rgbafloat42uchar4(make_float4(yuv2Rgb(yuv1), 1.f));
  }
}
```

这个代码就是GPU去处理的,也顺便解释下,前面1920为什么要除2,因为YUV是二个像素共用UV,如果不除,每个像素多算了一次UV.至于里面进位关系,参考下面手绘图.


同样,RGBA8BIT转YUV10BIT如下.

``` C++
void rgb2yuv_gpu(PtrStepSz<uchar4> source, PtrStepSz<uchar> dest,
                 int32_t yuvtype, cudaStream_t stream) {
    dim3 grid(divUp(source.width / 2, block.x), divUp(source.height, block.y));
    rgb2yuv10B<<<grid, block, 0, stream>>>(source, dest);
}
inline __global__ void rgb2yuv10B(PtrStepSz<uchar4> source,
                                  PtrStepSz<uchar> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  if (idx < source.width / 2 && idy < source.height) {
    float4 rgba1 = rgbauchar42float4(source(idy, idx * 2));
    float4 rgba2 = rgbauchar42float4(source(idy, idx * 2 + 1));
    float3 yuv1 = rgb2Yuv(make_float3(rgba1));
    float3 yuv2 = rgb2Yuv(make_float3(rgba2));
    // UYVY
    float4 ryuyv = make_float4((yuv1.y + yuv2.y) / 2.f, yuv1.x,
                               (yuv1.z + yuv2.z) / 2.f, yuv2.x);
    ushort4 syuyv = float42uint10(ryuyv);
    // 4个10有效位的数据分成5个uint8
    uint8_t b1 = (syuyv.x >> 2) & 0xFF;
    uint8_t b2 = ((syuyv.x << 6) | (syuyv.y >> 4)) & 0xFF;
    uint8_t b3 = ((syuyv.y << 4) | (syuyv.z >> 6)) & 0xFF;
    uint8_t b4 = ((syuyv.z << 2) | (syuyv.w >> 8)) & 0xFF;
    uint8_t b5 = syuyv.w & 0xFF;
    dest(idy, idx * 5) = b1;
    dest(idy, idx * 5 + 1) = b2;
    dest(idy, idx * 5 + 2) = b3;
    dest(idy, idx * 5 + 3) = b4;
    dest(idy, idx * 5 + 4) = b5;
  }
}
```

都用CUDA完成了,当然对应的Vulkan模块也要添加相应功能了,分组一样,只贴对应glsl compute shader实现代码.

``` c++
#version 450

// 16bit https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GL_EXT_shader_16bit_storage.txt
#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types_int16: require

layout (local_size_x = 16, local_size_y = 16) in;
layout (binding = 0, r8) uniform readonly image2D inTex;
layout (binding = 1, rgba8) uniform image2D outTex;
layout (binding = 2) uniform UBO {
	int width;
	int height;
	int yuvType;
} ubo;

vec4 yuv2Rgb(float y, float u, float v, float a) {
	vec4 xrgba = vec4(0.f);
	xrgba.r = clamp(y + 1.402f * v, 0.f, 1.f);
	xrgba.g = clamp(y - 0.71414f * v - 0.34414f * u, 0.f, 1.f);
	xrgba.b = clamp(y + 1.772f * u, 0.f, 1.f);
	xrgba.a = a;
	return xrgba;
}

const uint mask10bit = uint(1023);

void main(){
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outTex);
    if(uv.x >= size.x/2 || uv.y >= size.y){
        return;
    } 
	// 只有8位有效
	uint b1 = uint(imageLoad(inTex,ivec2(uv.x*5,uv.y)).r * 255);
	uint b2 = uint(imageLoad(inTex,ivec2(uv.x*5+1,uv.y)).r * 255);
	uint b3 = uint(imageLoad(inTex,ivec2(uv.x*5+2,uv.y)).r * 255);
	uint b4 = uint(imageLoad(inTex,ivec2(uv.x*5+3,uv.y)).r * 255);
	uint b5 = uint(imageLoad(inTex,ivec2(uv.x*5+4,uv.y)).r * 255);
	// 5个8位,一共40个有效位,对应四个10BIT的值
	float u = float(((b1 << 2) | (b2 >> 6)) & mask10bit)* 0.00097751710654936461f - 0.5f;
    float y1 = float(((b2 << 4) | (b3 >> 4)) & mask10bit)* 0.00097751710654936461f;
    float v = float(((b3 << 6) | (b4 >> 2)) & mask10bit)* 0.00097751710654936461f - 0.5f;
    float y2 = float(((b4 << 8) | (b5)) & mask10bit)* 0.00097751710654936461f;	
	vec4 rgba1 = yuv2Rgb(y1,u,v,1.f);
	vec4 rgba2 = yuv2Rgb(y2,u,v,1.f);
	imageStore(outTex, ivec2(uv.x*2,uv.y),rgba1); 
	imageStore(outTex, ivec2(uv.x*2+1,uv.y),rgba2); 
}
```

``` c++
#version 450

// 16bit https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GL_EXT_shader_16bit_storage.txt
#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types_int16: require

layout (local_size_x = 16, local_size_y = 16) in;
layout (binding = 0, rgba8) uniform readonly image2D inTex;
layout (binding = 1, r8) uniform image2D outTex;
layout (binding = 2) uniform UBO 
{
	int width;
	int height;
	int yuvType;
} ubo;

vec4 rgb2Yuv(vec4 rgba) {
	vec4 yuva;
	//xyz -> yuv
	yuva.x = clamp(0.299 * rgba.r + 0.587 * rgba.g + 0.114 * rgba.b, 0, 1);
	//uv (-0.5,0.5)
	yuva.y = clamp(-0.1687 * rgba.r - 0.3313 * rgba.g + 0.5 * rgba.b + 0.5f, 0, 1);
	yuva.z = clamp(0.5 * rgba.r - 0.4187 * rgba.g - 0.0813 * rgba.b + 0.5f, 0, 1);
	yuva.a = clamp(rgba.w, 0, 1);
	return yuva; 
}

vec4 make_vec4(uint r){
	float fr = float(r)*0.003921568627f;
    return vec4(fr,fr,fr,fr);
}

const uint mask10bit = uint(1023);

void main(){
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(inTex);
    if(uv.x >= size.x/2 || uv.y >= size.y){
        return;
    } 
	vec4 rgba1 = imageLoad(inTex,ivec2(uv.x*2,uv.y)).rgba;
    vec4 rgba2 = imageLoad(inTex,ivec2(uv.x*2+1,uv.y)).rgba;
	vec4 yuv1 = rgb2Yuv(rgba1);
	vec4 yuv2 = rgb2Yuv(rgba2);
	uint u = uint((yuv1.y + yuv2.y) / 2.f * mask10bit);
	uint y = uint(yuv1.x * mask10bit);
	uint v = uint((yuv1.z + yuv2.z) / 2.f * mask10bit);
	uint y1 = uint(yuv2.x * mask10bit);
	// 5个8位,一共40个有效位,对应四个10BIT的值
	uint b1 = (u >> 2) & 0xFF;
    uint b2 = ((u << 6) | (y >> 4)) & 0xFF;
    uint b3 = ((y << 4) | (v >> 6)) & 0xFF;
    uint b4 = ((v << 2) | (y1 >> 8)) & 0xFF;
    uint b5 = y1 & 0xFF;
	// 存入对应五个位置
	imageStore(outTex, ivec2(uv.x*5,uv.y),make_vec4(b1)); 
	imageStore(outTex, ivec2(uv.x*5+1,uv.y),make_vec4(b2)); 
	imageStore(outTex, ivec2(uv.x*5+2,uv.y),make_vec4(b3)); 
	imageStore(outTex, ivec2(uv.x*5+3,uv.y),make_vec4(b4)); 
	imageStore(outTex, ivec2(uv.x*5+4,uv.y),make_vec4(b5)); 
}
```

其实YUV10BIT最好转RGBA16BIT,这样图像显示出来没有损失,但是问题是现在所有滤镜默认中转都是RGBA8BIT来的,各位大佬有没好的思路,能相对方便简单把已经实现的超多滤镜层添加一个RGBA18输出结果?
