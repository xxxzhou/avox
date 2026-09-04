> 整理自 aocec 仓库 `doc/virtualproduction/XR拍摄-虚拟与真实相机混合.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# XR拍摄-虚拟与真实相机混合

先看案例:

首先是相机拍摄的实景.


下面标定后,虚拟摄像机同样位置下融合没有校正畸变的图.


下面标定后,虚拟摄像机同样位置融合校正畸变后的图.


可以看到,精准标定后,明明重投影误差在3像素左右,但是上面的融合效果可太差了.

## 畸变校正

首先分析下,直接混合有问题是正常的,但是畸变校正后,为啥还有问题了?可以对比上面二张图,校正的图虚拟相对真实的整体右下偏移了些像素.

稍微想下,因为UE里虚拟的相机,如果不做特殊处理,他的内参里cx,cy就是0.5,fy/fx=aspect-ratio,畸变系数k1,k2,p1,p2,k3全是0,而真实的相机因为制作工艺等各个原因,上面各参数都会有细小不同,如果不做处理,直接把虚拟的图与真实的图混合,在角点,边这些信息量高的位置上,人眼就能明显看到不合理之处,越是图像的周边部分其不合理的位置越明显.而校正后,其fy/fx=aspect-ratio,畸变系数k1,k2,p1,p2,k3也可等同0,但是cx,cy不做默认处理,不一定是在0.5,0.5的,所以就会出现如上的整体偏移.

那么如何把真实图像变成虚拟相机以圆心在绝对中间,无畸变的图完美混合?OpenCV里有相关畸变处理的函数,不过需要先想明白一个问题,图像畸变处理后,相对原图,处理后图像内参有没变化?首先很明显,畸变系数全是零了,去畸变是会导致图像边缘部分的丢弃,根据丢弃的边缘部分其最终图像的区域是有变化的,很明显,这会导致fx,fy的变化.可以使用getOptimalNewCameraMatrix确定畸变后内参模型,UE虚拟相机的FOV要以畸变后的内参模型来合成,其中心问题也可以用getOptimalNewCameraMatrix解决,先以真实图像匹配中心看看效果.

先看下,处理后的效果.


相应流程如下,首先得到畸变校正后内参模型.

``` C++
// 如果用于XR混合虚拟图像,需要把center设置为true,选择和虚拟图像一样中心是0.5/0.5无畸变的理想图,虽然选择的范围会更小
LensModel getDistortLensModel(const LensModel& lensModel, float alpha,
                              bool center) {
  LensModel destModel = lensModel;
  if (lensModel.imageSize.x == 0 || lensModel.imageSize.y == 0) {
    logMessage(LogLevel::warn,
               "getDistortLensModel lensModel must have corrert imageSize");
    return destModel;
  }
  // 摄像机内参
  cv::Mat cameraMatrix;
  // 畸变系数
  cv::Mat distCoeffs;
  cv::Size imageSize(lensModel.imageSize.x, lensModel.imageSize.y);
  lensModelToCV(lensModel, cameraMatrix, distCoeffs);
  // 得到校正畸变后的新摄像机内参
  cv::Mat newCameraMatrix = cv::getOptimalNewCameraMatrix(
      cameraMatrix, distCoeffs, imageSize, alpha, imageSize, nullptr, center);
  lensCVToModel(newCameraMatrix, distCoeffs, lensModel.imageSize, destModel);
  return destModel;
}
```

然后原内参与校正内参得到得到UV映射图,方便GPU处理,注意UE虚拟相机使用校正内参生成的FOV.

``` C++
bool getDistortBuffer(const LensModel& srcLensModel,
                      const LensModel& destLensModel, IImageBuffer* xMap,
                      IImageBuffer* yMap) {
  if (srcLensModel.imageSize.x == 0 || srcLensModel.imageSize.y == 0) {
    logMessage(LogLevel::warn,
               "getDistortBuffer lensModel must have corrert imageSize");
    return false;
  }
  ImageFormat format = {};
  format.width = srcLensModel.imageSize.x;
  format.height = srcLensModel.imageSize.y;
  format.imageType = ImageType::r32f;
  xMap->setImageFormat(format);
  yMap->setImageFormat(format);
  // 畸变UV映射图
  cv::Size imageSize(srcLensModel.imageSize.x, srcLensModel.imageSize.y);
  cv::Mat xMapMat(srcLensModel.imageSize.x, srcLensModel.imageSize.y, CV_32FC1);
  cv::Mat yMapMat(srcLensModel.imageSize.x, srcLensModel.imageSize.y, CV_32FC1);
  // 摄像机内参
  cv::Mat srcCameraMatrix, destCameraMatrix;
  // 畸变系数
  cv::Mat srcDistCoeffs, destDistCoeffs;
  lensModelToCV(srcLensModel, srcCameraMatrix, srcDistCoeffs);
  lensModelToCV(destLensModel, destCameraMatrix, destDistCoeffs);
  // initInverseRectificationMap添加畸变
  cv::initUndistortRectifyMap(srcCameraMatrix, srcDistCoeffs, cv::noArray(),
                              destCameraMatrix, imageSize, xMapMat.type(),
                              xMapMat, yMapMat);
  // 复制出结果
  memcpy(xMap->getPointer(), xMapMat.data, getImageSize(format));
  memcpy(yMap->getPointer(), yMapMat.data, getImageSize(format));
  return true;
}
```

最后使用Vulkan+CS实现畸变处理.

``` glsl
#version 450

layout(local_size_x = 16, local_size_y = 16) in; // gl_WorkGroupSize

// ADDMAP 相当于原图UV添加MAP后,如添加畸变,反正则去畸变
layout(binding = 0) uniform sampler2D inSampler;
layout(binding = 1) uniform sampler2D inXSampler;
layout(binding = 2) uniform sampler2D inYSampler;

layout(binding = 3, rgba8) uniform image2D outTex;

layout(std140, binding = 4) uniform UBO {
  int mapWidth;
  int mapHeight;
}
ubo;

void main() {
  ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
  ivec2 size = imageSize(outTex);
  if (uv.x >= size.x || uv.y >= size.y) {
    return;
  }
  vec2 suv = (vec2(uv) + vec2(0.5f)) / vec2(size);
  float xalpha = textureLod(inXSampler, suv, 0).r;
  float yalpha = textureLod(inYSampler, suv, 0).r;
  // 正常图UV对应的畸变图上UV
  vec2 mapUV =
      (vec2(xalpha, yalpha) + vec2(0.5)) / vec2(ubo.mapWidth, ubo.mapHeight);
#if ADDMAP
  // 添加畸变(在正常的图取正常UV的颜色放入畸变UV图上) distort
  // 畸变UV做为输出,很难处理,现参考UE4做法(近似正常,小计算量)
  mapUV = suv - mapUV;
#endif
  vec2 clampedUV = clamp(mapUV, vec2(0.0), vec2(1.0));
  vec2 isOutside = step(mapUV, vec2(0.0)) + step(vec2(1.0), mapUV);
  vec4 color = textureLod(inSampler, clampedUV, 0);
  vec4 result = mix(color, vec4(0.0), dot(isOutside, vec2(1.0))); 
  // vec4 result = textureLod(inSampler, mapUV,0); 
  imageStore(outTex, uv, result);
}
```

组合数据输入成执行管线,根据其输入处理完后就可以给引擎显示,其输入(UE里相机渲染图)也可以是UE里的DX11/DX12纹理,具体交互可看[Vulkan与DX11交互](../../player/多平台GPU共享.md)

## 像素偏移

如果畸变以中心匹配,会发现一个问题,图像裁剪了一些,这是校正畸变必需的,但是如果能裁剪少一些,除了alpha参数,不以中心匹配也会好很多,而中心偏移的像素根据cx,cy其实可以直接算出来,只需要根据得去需要移动的像素,然后把生成的虚拟图像偏移下就行.

先看效果,相对上面的匹配图,看到范围明显大了.


先计算畸变内参与校正内参引起的像素平移值.

``` C++
void FBlendGraph::UpdateLensModel(const aoce::LensModel& LensModelT, const aoce::LensModel& DestLensModelT)
{
	LensModel = LensModelT;
	DestLensModel = DestLensModelT;
	bool bOldDistortion = bDistortion;
	// 如果二者相等,说明不需要校正畸变
	bDistortion = !(LensModel.focalLength == DestLensModelT.focalLength);
	// 得到UV映射图
	if (bDistortion) { getDistortBuffer(LensModel, DestLensModel, MapX.get(), MapY.get()); }
	// 虚拟图像中心需要平移像素
	CropPar.left = 0.5f - DestLensModelT.focalCenter.x;
	CropPar.top = 0.5f - DestLensModelT.focalCenter.y;
	bool bOldPixelOffset = bPixelOffset;
	bPixelOffset = std::abs(CropPar.left) < 0.001 && std::abs(CropPar.top) < 0.001;	
	// 畸变校正改变后,需要重新生成管线
	if (bDistortion != bOldDistortion || bPixelOffset != bOldPixelOffset) { ResetPipeGraph(); };
	if (bDistortion) { UVMapLayer->updateMap(MapX.get(), MapY.get()); }
}
```

使用Vulkan+CS实现像素偏移.

``` glsl
#version 450

layout(local_size_x = 16, local_size_y = 16) in; // gl_WorkGroupSize

layout(binding = 0) uniform sampler2D inSampler;
layout(binding = 1, rgba8) uniform image2D outTex;

layout(std140, binding = 2) uniform UBO {
  float leftOffset;
  float topOffset;
  float x;
  float y;
  float z;
  float w;
}
ubo;

void main() {
  ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
  ivec2 size = imageSize(outTex);
  if (uv.x >= size.x || uv.y >= size.y) {
    return;
  }
  vec2 suv = (vec2(uv) + vec2(0.5f)) / vec2(size) +
             vec2(ubo.leftOffset, ubo.topOffset);
  vec2 clampedUV = clamp(suv, vec2(0.0), vec2(1.0));
  // UV是否越界
  vec2 isOutside = step(suv, vec2(0.0)) + step(vec2(1.0), suv);
  vec4 color = textureLod(inSampler, clampedUV, 0);
  // 
  vec4 fillColor = vec4(ubo.x,ubo.y,ubo.z,ubo.w);
  vec4 result = mix(color, fillColor, dot(isOutside, vec2(1.0)));  
  imageStore(outTex, uv, result);
}
```

到这用来匹配标定精度够了,后面XR还需要加上把虚拟的图像畸变化,以匹配真实图像才更完美.
