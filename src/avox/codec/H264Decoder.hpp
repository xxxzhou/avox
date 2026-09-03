#pragma once

#include "../AvoxCodec.h"
#include "../AvoxMath.h"
#include "../source/PacketBuf.hpp"
#include "H264Parse.hpp"
#include "H26XHelper.hpp"

namespace avox {

// 通用的H264分析配置包,帮助解析SPS
// 对比VulkanH264Parser.cpp
class H264Decoder {
 public:
  H264Decoder();
  virtual ~H264Decoder();

 protected:
  std::unique_ptr<H264Parse> parse = nullptr;
  bool bConfig = false;

  int32_t curId = 0;
  H264PicturePtr prePicture = nullptr;
  H264PicturePtr curPicture = nullptr;

  std::vector<H264PicturePtr> pictures;
  std::vector<H264PicturePtr> refPictures0;
  std::vector<H264PicturePtr> refPictures1;

  // 保持当前解码的帧信息
  H264SliceHeaderPtr slice = nullptr;
  H264SpsPtr sps = nullptr;
  H264PpsPtr pps = nullptr;
  H264NalUnitPtr unit = nullptr;

  DecoderParams decoderParams = {};

 protected:
  // 当配置成功后 bInit表示初始化还是更新
  virtual void onConfigChange(bool bInit) {};
  virtual void onBeginDecode() {};
  virtual void onEndDecode() {};

 protected:
  bool parsePacket(const AvoxPacket & packet);
  void beginDecode();
  void endDecode();

 protected:
  //  根据图像顺序计数(POC)类型来解码当前图像的POC值
  void decodingProcessForPictureOrderCount();
  void decodeH264PictureOrderCountType0();
  void decodeH264PictureOrderCountType1();
  void decodeH264PictureOrderCountType2();

  // 解码参考图像列表的构建过程
  void decodingProcessForReferencePictureListsConstruction();
  // 解码图像编号的过程
  void decodingProcessForPictureNumbers();
  // 初始化参考图像列表的过程
  void initializationProcessForReferencePictureLists();
  // 修改参考图像列表的过程
  void modificationProcessForReferencePictureLists(
      uint32_t& refIdxLX, std::vector<H264PicturePtr>& RefPicListX,
      uint32_t num_ref_idx_lX_active_minus1);
  H264PicturePtr findPictureByPicNum(int32_t pic_num);
  H264PicturePtr findPictureByLongTermPicNum(int32_t long_term_pic_num);

  // 解码参考图像标记的过程
  void decodeReferencePictureMarkingProcess();
  // 更新参考图片的标记状态
  void updateReferenceFlag(H264PicturePtr picture, uint8_t flag,
                           bool append = true);
  // 滑动窗口解码参考图片标记过程
  void slidingWindowDecodedReferencePictureMarkingProcess();
  // 自适应内存控制解码参考图片标记过程
  void adaptiveMemoryControlDecodedReferencePicutreMarkingPorcess();
  // 取消标记为短期参考的图片
  void unMarkUsedForShortTermReference(int32_t picNumx);
  // 取消标记为长期参考的图片
  void unMarkUsedForLongTermReference(int32_t long_term_pic_num);
  // 取消标记为参考的图片
  void unMarkUsedForReference(int32_t long_term_pic_num);
  // 将短期参考图片标记为长期参考图片
  void markShortTermReferenceToLongTermReference(int32_t picNumx,
                                                 uint32_t long_term_frame_idx);
  // 查找互补图片
  H264PicturePtr findComplementaryPicture(H264PicturePtr picture);
};

}