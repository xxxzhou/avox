#include "H264Decoder.hpp"

#include <algorithm>
#include <deque>
namespace avox {

static int32_t GetPicNumX(H264SliceHeaderPtr slice,
                          uint32_t difference_of_pic_nums_minus1) {
  int32_t picNumX = 0;
  {
    // The variable CurrPicNum is derived as follows:
    // - If field_pic_flag is equal to 0, CurrPicNum is set equal to frame_num.
    // - Otherwise (field_pic_flag is equal to 1), CurrPicNum is set equal to 2
    // * frame_num + 1
    uint64_t CurrPicNum = 0;
    if (slice->field_pic_flag == 0) {
      CurrPicNum = slice->frame_num;
    } else if (slice->field_pic_flag == 1) {
      // CurrPicNum = 2 * slice->frame_num + 1;
      // Hint : not support for now
      assert(false);
    }
    picNumX =
        (int32_t)(CurrPicNum - (difference_of_pic_nums_minus1 + 1)); // (8-39)
  }
  return picNumX;
}

H264Decoder::H264Decoder() {
  parse = std::make_unique<H264Parse>();
  bConfig = false;
}

H264Decoder::~H264Decoder() {}

bool H264Decoder::parsePacket(const AvoxPacket &packet) {
  bool bParse = parse->parse(packet.data.data, packet.data.size);
  unit = parse->curUnit;
  if (!bParse) {
    log(LogLevel::warn, "nula parse error:", getNalName(unit->nal),
        " size:", packet.data.size);
    return false;
  }
  H264ContextPtr h264Context = parse->h264Contex;
  sps = h264Context->sps;
  pps = h264Context->pps;
  // 如果已经成功解析pps/sps
  if (pps && sps && unit->configFrame()) {
    decoderParams.yuvType = getYuvType(sps->chroma_format_idc);
    vec2i size = getSize(*sps);
    decoderParams.width = size.x;
    decoderParams.height = size.y;
    decoderParams.fps = getFpsFromSps(*sps);
    decoderParams.yBitDepth = sps->bit_depth_luma_minus8 + 8;
    decoderParams.uvBitDepth = sps->bit_depth_chroma_minus8 + 8;
    log(LogLevel::info, "h264Decoder desc: ", decoderParams,
        " yBitDepth:", decoderParams.yBitDepth,
        " uvBitDepth:", decoderParams.uvBitDepth);
    // 第一次传入true,表示初始化,后续表明是更新参数
    onConfigChange(!bConfig);
    bConfig = true;
  }
  // config解析后，才能解码编码帧
  if (!bConfig) {
    return true;
  }
  if (!parse->enableParseSLICE) {
    return true;
  }
  // 解析切片
  if (unit->nal == H264NAL::NAL_IDR || unit->nal == H264NAL::NAL_B_P) {
    // H264SpsSyntax& pps =
    slice = parse->curSlice;
    if (!h264Context->ppsSet.count(slice->pic_parameter_set_id)) {
      LOGFLF(LogLevel::warn, "pps:", slice->pic_parameter_set_id, " not found");
      return false;
    }
    pps = h264Context->ppsSet[slice->pic_parameter_set_id];
    if (!h264Context->spsSet.count(pps->seq_parameter_set_id)) {
      LOGFLF(LogLevel::warn, "sps:", slice->pic_parameter_set_id, " not found");
      return false;
    }
    sps = h264Context->spsSet[pps->seq_parameter_set_id];
    // 不支持图像计算类型超过2的值
    if (sps->pic_order_cnt_type > 2) {
      LOGFLF(LogLevel::warn, "pic_order_cnt_type:", sps->pic_order_cnt_type,
             " not support");
      return false;
    }
    curPicture = std::make_shared<H264Picture>();
    curPicture->field_pic_flag = slice->field_pic_flag;
    curPicture->bottom_field_flag = slice->bottom_field_flag;
    curPicture->pic_order_cnt_lsb = slice->pic_order_cnt_lsb;
    curPicture->FrameNum = slice->frame_num;
    beginDecode();
    // 调用解码图片顺序计数类型的函数，根据pic_order_cnt_type确定算法。
    decodingProcessForPictureOrderCount();
    // 到IDR帧清空所有的参考帧,重新设置参考
    if (unit->nal == H264NAL::NAL_IDR) {
      pictures.clear();
    } else if (slice->slice_type == H264SliceType::MMP_H264_P_SLICE ||
               slice->slice_type == H264SliceType::MMP_H264_B_SLICE ||
               slice->slice_type == H264SliceType::MMP_H264_SP_SLICE) {
      // 调用解码参考图片列表构建的函数，根据当前图片的类型和参考图片的信息，构建参考图片列表。
      decodingProcessForReferencePictureListsConstruction();
    }
    // 调用解码参考图片标记过程的函数，根据当前图片的类型和参考图片的信息，标记参考图片的使用情况。
    decodeReferencePictureMarkingProcess();
    endDecode();
    log(LogLevel::info, "picture id:", PicOrderCnt(curPicture),
        " framenum:", curPicture->FrameNum, " PicNum:", curPicture->PicNum,
        " MaxFrame:", curPicture->MaxFrameNum,
        " frameWrap:", curPicture->FrameNumWrap);
    curPicture->id = curId++;
    bool found = false;
    for (auto &picture : pictures) {
      if (picture == curPicture) {
        found = true;
        break;
      }
    }
    if (!found) {
      pictures.push_back(curPicture);
    }
    prePicture = curPicture;
  }
  return true;
}

void H264Decoder::beginDecode() { onBeginDecode(); }

void H264Decoder::endDecode() {
  if (curPicture->has_memory_management_control_operation_5) {
    int32_t tempPicOrderCnt = PicOrderCnt(curPicture);
    curPicture->TopFieldOrderCnt =
        curPicture->TopFieldOrderCnt - tempPicOrderCnt;
    curPicture->BottomFieldOrderCnt =
        curPicture->BottomFieldOrderCnt - tempPicOrderCnt;
  }
  // 重新记录引用的pictures
  std::vector<H264PicturePtr> tempPictures;
  for (auto picture : pictures) {
    if ((picture->referenceFlag & used_for_short_term_reference) ||
        (picture->referenceFlag & used_for_long_term_reference)) {
      tempPictures.push_back(picture);
    }
  }
  pictures.swap(tempPictures);
  onEndDecode();
}

void H264Decoder::decodingProcessForPictureOrderCount() {
  if (sps->pic_order_cnt_type == 0) {
    decodeH264PictureOrderCountType0();
  } else if (sps->pic_order_cnt_type == 1) {
    decodeH264PictureOrderCountType1();
  } else if (sps->pic_order_cnt_type == 2) {
    decodeH264PictureOrderCountType2();
  }
}

void H264Decoder::decodeH264PictureOrderCountType0() {
  // 图片顺序计数(Picture Order Count, POC)
  // 用于存储前一帧的 POC 的高位部分。
  int32_t prevPicOrderCntMsb = 0;
  // 用于存储前一帧的 POC 的低位部分。
  uint32_t prevPicOrderCntLsb = 0;
  int32_t PicOrderCntMsb = 0;
  // determine prevPicOrderCntMsb and prevPicOrderCntLsb
  if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE) {
    prevPicOrderCntMsb = 0;
    prevPicOrderCntLsb = 0;
  } else {
    if (prePicture->has_memory_management_control_operation_5) {
      if (!prePicture->bottom_field_flag) {
        prevPicOrderCntMsb = 0;
        prevPicOrderCntLsb = prePicture->TopFieldOrderCnt;
      } else {
        prevPicOrderCntMsb = 0;
        prevPicOrderCntLsb = 0;
      }
    } else {
      prevPicOrderCntMsb = prePicture->prevPicOrderCntMsb;
      prevPicOrderCntLsb = prePicture->pic_order_cnt_lsb;
    }
  }
  // determine PicOrderCntMsb (8-3)
  uint32_t MaxPicOrderCntLsb = sps->context.MaxPicOrderCntLsb;
  if ((slice->pic_order_cnt_lsb < prevPicOrderCntLsb) &&
      ((prevPicOrderCntLsb - slice->pic_order_cnt_lsb) >=
       (MaxPicOrderCntLsb / 2))) {
    PicOrderCntMsb = (int32_t)(prevPicOrderCntMsb + MaxPicOrderCntLsb);
  } else if ((slice->pic_order_cnt_lsb > prevPicOrderCntLsb) &&
             ((slice->pic_order_cnt_lsb - prevPicOrderCntLsb) >
              (MaxPicOrderCntLsb / 2))) {
    PicOrderCntMsb = (int32_t)(prevPicOrderCntMsb - MaxPicOrderCntLsb);
  } else {
    PicOrderCntMsb = prevPicOrderCntMsb;
  }

  // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-4) and (8-5)
  curPicture->TopFieldOrderCnt = PicOrderCntMsb + slice->pic_order_cnt_lsb;
  if (pps->bottom_field_pic_order_in_frame_present_flag &&
      !slice->field_pic_flag) {
    curPicture->BottomFieldOrderCnt =
        curPicture->TopFieldOrderCnt + slice->delta_pic_order_cnt_bottom;
  } else {
    curPicture->BottomFieldOrderCnt = PicOrderCntMsb + slice->pic_order_cnt_lsb;
  }
  // update context
  if (unit->RefIdc != 0) {
    curPicture->prevPicOrderCntMsb = PicOrderCntMsb;
  } else {
    curPicture->prevPicOrderCntMsb = prePicture->prevPicOrderCntMsb;
  }
}

/**
 * @sa ISO 14496/10(2020) - 8.2.1.2 Decoding process for picture order count
 * type 1
 */
void H264Decoder::decodeH264PictureOrderCountType1() {
  uint32_t prevFrameNum = prePicture->FrameNum;
  int32_t prevFrameNumOffset = 0;
  int64_t absFrameNum = 0;
  int64_t picOrderCntCycleCnt = 0;
  int64_t frameNumInPicOrderCntCycle = 0;
  int64_t expectedPicOrderCnt;
  // determine prevFrameNumOffset
  if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE) {
    if (prePicture->has_memory_management_control_operation_5) {
      prevFrameNumOffset = 0;
    } else {
      prevFrameNumOffset = (int32_t)(prePicture->FrameNumOffset);
    }
  }
  // determine FrameNumOffset (8-6)
  if (unit->nal == H264NAL::NAL_IDR) {
    // (7-0)
    curPicture->FrameNumOffset = 0;
  } else if (prevFrameNum > slice->frame_num) {
    uint32_t MaxFrameNum = sps->context.MaxFrameNum;
    curPicture->FrameNumOffset = prevFrameNumOffset + MaxFrameNum;
  } else {
    curPicture->FrameNumOffset = prevFrameNumOffset;
  }

  // determine absFrameNum (8-7)
  if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0) {
    absFrameNum = curPicture->FrameNumOffset + slice->frame_num;
  } else {
    absFrameNum = 0;
  }
  if (unit->RefIdc == 0 && absFrameNum > 0) {
    absFrameNum = absFrameNum - 1;
  }
  // determine picOrderCntCycleCnt and frameNumInPicOrderCntCycle (8-8)
  if (absFrameNum > 0) {
    picOrderCntCycleCnt =
        (absFrameNum - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
    frameNumInPicOrderCntCycle =
        (absFrameNum - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;
  }
  // determine expectedPicOrderCnt (8-9)
  if (absFrameNum > 0) {
    int64_t ExpectedDeltaPerPicOrderCntCycle =
        sps->context.ExpectedDeltaPerPicOrderCntCycle;
    expectedPicOrderCnt =
        picOrderCntCycleCnt * ExpectedDeltaPerPicOrderCntCycle;
    for (int64_t i = 0; i <= frameNumInPicOrderCntCycle; i++) {
      expectedPicOrderCnt =
          expectedPicOrderCnt + sps->offset_for_ref_frame[(size_t)i];
    }
  } else {
    expectedPicOrderCnt = 0;
  }
  if (unit->RefIdc == 0) {
    expectedPicOrderCnt = expectedPicOrderCnt + sps->offset_for_non_ref_pic;
  }

  // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-10)
  if (!slice->field_pic_flag) {
    curPicture->TopFieldOrderCnt =
        (int32_t)(expectedPicOrderCnt + slice->delta_pic_order_cnt[0]);
    curPicture->BottomFieldOrderCnt = curPicture->TopFieldOrderCnt +
                                      sps->offset_for_top_to_bottom_field +
                                      slice->delta_pic_order_cnt[1];
  } else if (!slice->bottom_field_flag) {
    curPicture->TopFieldOrderCnt =
        (int32_t)(expectedPicOrderCnt + slice->delta_pic_order_cnt[0]);
  } else {
    curPicture->BottomFieldOrderCnt =
        (int32_t)(expectedPicOrderCnt + sps->offset_for_top_to_bottom_field +
                  slice->delta_pic_order_cnt[0]);
  }
}

/**
 * @sa ISO 14496/10(2020) - 8.2.1.3 Decoding process for picture order count
 * type 2
 */
void H264Decoder::decodeH264PictureOrderCountType2() {
  int64_t FrameNumOffset = 0;
  int64_t tempPicOrderCnt = 0;
  uint64_t prevFrameNumOffset = 0;
  // determine prevFrameNumOffset
  if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE) {
    if (prePicture->has_memory_management_control_operation_5) {
      prevFrameNumOffset = 0;
    } else {
      prevFrameNumOffset = prePicture->FrameNumOffset;
    }
  }
  // determine FrameNumOffset (8-11)
  if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE) {
    FrameNumOffset = 0;
  } else if (prevFrameNumOffset > slice->frame_num) {
    uint32_t MaxFrameNum = sps->context.MaxFrameNum;
    FrameNumOffset = prevFrameNumOffset + MaxFrameNum;
  } else {
    FrameNumOffset = prevFrameNumOffset;
  }
  curPicture->FrameNumOffset = FrameNumOffset;
  // determine tempPicOrderCnt (8-12)
  if (slice->slice_type == H264SliceType::MMP_H264_I_SLICE) {
    tempPicOrderCnt = 0;
  } else if (unit->RefIdc == 0) {
    tempPicOrderCnt = 2 * (FrameNumOffset + slice->frame_num) - 1;
  } else {
    tempPicOrderCnt = 2 * (FrameNumOffset + slice->frame_num);
  }
  // determine TopFieldOrderCnt and BottomFieldOrderCnt (8-13)
  if (!slice->field_pic_flag) {
    curPicture->TopFieldOrderCnt = (int32_t)tempPicOrderCnt;
    curPicture->BottomFieldOrderCnt = (int32_t)tempPicOrderCnt;
  } else if (slice->bottom_field_flag) {
    curPicture->BottomFieldOrderCnt = (int32_t)tempPicOrderCnt;
  } else {
    curPicture->TopFieldOrderCnt = (int32_t)tempPicOrderCnt;
  }
}

void H264Decoder::decodingProcessForReferencePictureListsConstruction() {
  decodingProcessForPictureNumbers();
  initializationProcessForReferencePictureLists();
  // if (slice->rplm.ref_pic_list_modification_flag_l0) {
  //   uint32_t refIdxL0 = 0;
  //   modificationProcessForReferencePictureLists(
  //       refIdxL0, refPictures0, slice->num_ref_idx_l0_active_minus1);
  // }
  // if (slice->rplm.ref_pic_list_modification_flag_l1 &&
  //     slice->slice_type == H264SliceType::MMP_H264_B_SLICE) {
  //   uint32_t refIdxL1 = 0;
  //   modificationProcessForReferencePictureLists(
  //       refIdxL1, refPictures1, slice->num_ref_idx_l1_active_minus1);
  // }
}

void H264Decoder::decodingProcessForPictureNumbers() {
  uint32_t MaxFrameNum = sps->context.MaxFrameNum;
  // FrameNumWrap用于短期参考图像，一个GOP一个周期，越大和当前图像越近
  for (auto &picture : pictures) {
    if (picture->referenceFlag & used_for_short_term_reference) {
      // 如果大于当前图像帧号
      if (picture->FrameNum > slice->frame_num) {
        picture->FrameNumWrap =
            (int32_t)picture->FrameNum - (int32_t)MaxFrameNum;
      } else {
        picture->FrameNumWrap = picture->FrameNum;
      }
    }
  }
  // PicNum短期参考图像帧号，LongTermPicNum长期参考图像帧号
  for (auto &picture : pictures) {
    if (picture->field_pic_flag == 0) {
      if (picture->referenceFlag & used_for_short_term_reference) {
        picture->PicNum = picture->FrameNumWrap; // (8-28)
      }
      if (picture->referenceFlag & used_for_long_term_reference) {
        picture->LongTermPicNum = (uint32_t)picture->LongTermFrameIdx; // (8-29)
      }
    } else if (picture->field_pic_flag == 1) {
      // 不支持场编码
      LOGFLF(LogLevel::warn, "h264 field_pic_flag not support");
      assert(false);
    }
  }
}

void H264Decoder::initializationProcessForReferencePictureLists() {
  refPictures0.clear();
  refPictures1.clear();
  // 8.2.4.2.1 Initialization process for the reference picture list for P and
  // SP slices in frames
  // https://www.cnblogs.com/TaigaCon/p/3715276.html
  // P帧排序,短期PicNum大的在前，长期LongTermPicNum大的在后
  if ((slice->slice_type == H264SliceType::MMP_H264_P_SLICE ||
       slice->slice_type == H264SliceType::MMP_H264_SP_SLICE) &&
      slice->field_pic_flag == 0) {
    // the highest PicNum value in descending order
    std::vector<H264PicturePtr> shortTermRefPicList;
    // the lowest LongTermPicNum value in ascending order
    std::vector<H264PicturePtr> longTermRefList;
    for (const H264PicturePtr &picture : pictures) {
      if (picture->referenceFlag & used_for_short_term_reference) {
        shortTermRefPicList.push_back(picture);
      }
      if (picture->referenceFlag & used_for_long_term_reference) {
        longTermRefList.push_back(picture);
      }
    }
    // 短期参考图像，越大和当前图像越近
    std::sort(
        shortTermRefPicList.begin(), shortTermRefPicList.end(),
        [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
          return left->PicNum > right->PicNum;
        });
    // 长期参考图像，越小和当前图像越近
    std::sort(
        longTermRefList.begin(), longTermRefList.end(),
        [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
          return left->LongTermPicNum < right->LongTermPicNum;
        });
    for (auto &shortTermPicture : shortTermRefPicList) {
      refPictures0.push_back(shortTermPicture);
    }
    for (auto &longTermPicture : longTermRefList) {
      refPictures0.push_back(longTermPicture);
    }
  }
  // 8.2.4.2.2 Initialization process for the reference picture list for P and
  // SP slices in fields
  else if ((slice->slice_type == H264SliceType::MMP_H264_P_SLICE ||
            slice->slice_type == H264SliceType::MMP_H264_SP_SLICE) &&
           slice->field_pic_flag == 1) {
    // Hint : not support field for now
    assert(false);
  }
  // 8.2.4.2.3 Initialization process for reference picture lists for B slices
  // in frames
  // B 帧排序 refPicList0 短期从小到大curpos,长期从小到大
  //  refPicList0 长期从大到小curpos,长期从小到
  else if ((slice->slice_type == H264SliceType::MMP_H264_B_SLICE) &&
           slice->field_pic_flag == 0) {
    int32_t curPoc = PicOrderCnt(curPicture);
    { // short term : PicOrderCnt( entryShortTerm ) less than
      // PicOrderCnt( CurrPic ) in descending order short term :  others in
      // ascending order
      std::deque<H264PicturePtr> RefPicList01;
      std::vector<H264PicturePtr> RefPicList02;
      // long term  :  the lowest LongTermPicNum value in ascending order
      std::vector<H264PicturePtr> RefPicList03;

      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_short_term_reference) {
          if (PicOrderCnt(picture) < curPoc) {
            RefPicList01.push_back(picture);
          }
        }
      }
      std::sort(
          RefPicList01.begin(), RefPicList01.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return PicOrderCnt(left) > PicOrderCnt(right);
          });

      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_short_term_reference) {
          if (PicOrderCnt(picture) > curPoc) {
            RefPicList02.push_back(picture);
          }
        }
      }
      std::sort(
          RefPicList02.begin(), RefPicList02.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return PicOrderCnt(left) < PicOrderCnt(right);
          });

      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_long_term_reference) {
          RefPicList03.push_back(picture);
        }
      }
      std::sort(
          RefPicList03.begin(), RefPicList03.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return left->LongTermPicNum < right->LongTermPicNum;
          });

      for (const auto &RefPic : RefPicList01) {
        refPictures0.push_back(RefPic);
      }
      for (const auto &RefPic : RefPicList02) {
        refPictures0.push_back(RefPic);
      }
      for (const auto &RefPic : RefPicList03) {
        refPictures0.push_back(RefPic);
      }
      // short term : PicOrderCnt( entryShortTerm ) greater than PicOrderCnt(
      // CurrPic ) in descending order
      std::vector<H264PicturePtr> RefPicList11;
      // short term :  others in ascending order
      std::vector<H264PicturePtr> RefPicList12;
      // long term  :  the lowest LongTermPicNum value in ascending order
      std::vector<H264PicturePtr> RefPicList13;
      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_short_term_reference) {
          int32_t picPoc = PicOrderCnt(picture);
          if (picPoc > curPoc) {
            RefPicList11.push_back(picture);
          }
        }
      }
      std::sort(
          RefPicList11.begin(), RefPicList11.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return PicOrderCnt(left) < PicOrderCnt(right);
          });

      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_short_term_reference) {
          int32_t picPoc = PicOrderCnt(picture);
          if (picPoc < curPoc) {
            RefPicList12.push_back(picture);
          }
        }
      }
      std::sort(
          RefPicList12.begin(), RefPicList12.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return PicOrderCnt(left) > PicOrderCnt(right);
          });

      for (auto picture : pictures) {
        if (picture->referenceFlag & used_for_long_term_reference) {
          RefPicList13.push_back(picture);
        }
      }
      std::sort(
          RefPicList13.begin(), RefPicList13.end(),
          [](const H264PicturePtr &left, const H264PicturePtr &right) -> bool {
            return left->LongTermPicNum > right->LongTermPicNum;
          });

      for (const auto &RefPic : RefPicList11) {
        refPictures1.push_back(RefPic);
      }
      for (const auto &RefPic : RefPicList12) {
        refPictures1.push_back(RefPic);
      }
      for (const auto &RefPic : RefPicList13) {
        refPictures1.push_back(RefPic);
      }
    }
  } else if ((slice->slice_type == H264SliceType::MMP_H264_B_SLICE) &&
             slice->field_pic_flag == 1) {
    // Hint : not support for now
    assert(false);
  }
  refPictures0.resize(slice->num_ref_idx_l0_active_minus1 + 1);
  refPictures1.resize(slice->num_ref_idx_l1_active_minus1 + 1);
}

void H264Decoder::modificationProcessForReferencePictureLists(
    uint32_t &refIdxLX, std::vector<H264PicturePtr> &RefPicListX,
    uint32_t num_ref_idx_lX_active_minus1) {
  uint64_t MaxPicNum = 0;
  uint64_t CurrPicNum = 0;
  int64_t picNumLX = 0;
  size_t index = 0;
  // determine CurrPicNum
  // The variable CurrPicNum is derived as follows:
  // - If field_pic_flag is equal to 0, CurrPicNum is set equal to frame_num.
  // - Otherwise (field_pic_flag is equal to 1), CurrPicNum is set equal to 2 *
  // frame_num + 1.

  if (slice->field_pic_flag == 0) {
    CurrPicNum = slice->frame_num;
  } else if (slice->field_pic_flag == 1) {
    // CurrPicNum = 2 * slice->frame_num + 1;
    // Hint : not support for now
    assert(false);
  }

  // determine MaxPicNum
  // The variable MaxPicNum is derived as follows:
  // - If field_pic_flag is equal to 0, MaxPicNum is set equal to MaxFrameNum.
  // - Otherwise (field_pic_flag is equal to 1), MaxPicNum is set equal to
  // 2*MaxFrameNum.
  {
    uint32_t MaxFrameNum = sps->context.MaxFrameNum;
    if (slice->field_pic_flag == 0) {
      MaxPicNum = MaxFrameNum;
    } else if (slice->field_pic_flag == 1) {
      // MaxPicNum = 2 * MaxFrameNum;
      // Hint : not support for now
      assert(false);
    }
  }

  uint64_t picNumLXPred = 0;
  bool initPicNumLXPred = false;
  for (uint32_t modification_of_pic_nums_idc :
       slice->rplm.modification_of_pic_nums_idcs) {
    // 8.2.4.3.1 Modification process of reference picture lists for
    // short-term reference pictures
    if (modification_of_pic_nums_idc == 0 ||
        modification_of_pic_nums_idc == 1) {
      // Hint :
      //   When the process specified in this subclause
      // is invoked the first time for a slice (that is, for the first
      // occurrence of modification_of_pic_nums_idc equal to 0 or 1 in the
      // ref_pic_list_modification( ) syntax), picNumL0Pred and
      // picNumL1Pred are initially set equal to CurrPicNum.
      if (initPicNumLXPred == false) {
        picNumLXPred = CurrPicNum;
        initPicNumLXPred = true;
      }
      uint64_t picNumLXNoWrap = 0;
      uint32_t abs_diff_pic_num_minus1 =
          slice->rplm.modification_of_pic_nums_idcs_datas[index++]
              .abs_diff_pic_num_minus1;
      // determine picNumLXNoWrap
      {
        if (modification_of_pic_nums_idc == 0) // (8-34)
        {
          if ((int64_t)picNumLXPred - (abs_diff_pic_num_minus1 + 1) < 0) {
            picNumLXNoWrap =
                picNumLXPred - (abs_diff_pic_num_minus1 + 1) + MaxPicNum;
          } else {
            picNumLXNoWrap = picNumLXPred - (abs_diff_pic_num_minus1 + 1);
          }
        } else if (modification_of_pic_nums_idc == 1) // (8-35)
        {
          if (picNumLXPred + (abs_diff_pic_num_minus1 + 1) >= MaxPicNum) {
            picNumLXNoWrap =
                picNumLXPred + (abs_diff_pic_num_minus1 + 1) - MaxPicNum;
          } else {
            picNumLXNoWrap = picNumLXPred + (abs_diff_pic_num_minus1 + 1);
          }
        }
      }
      // Hint : After each assignment of picNumLXNoWrap, the value of
      // picNumLXNoWrap is assigned to picNumLXPred.
      picNumLXPred = picNumLXNoWrap;
      // determine picNumLX (8-36)
      {
        if (picNumLXNoWrap > CurrPicNum) {
          picNumLX = picNumLXNoWrap - MaxPicNum;
        } else {
          picNumLX = picNumLXNoWrap;
        }
      }
      // (8-37)
      {
        auto PicNumF = [MaxPicNum](H264PicturePtr picture) -> int64_t {
          // Hint :
          // The function PicNumF( RefPicListX[ cIdx ] ) is derived as
          // follows:
          // - If the picture RefPicListX[ cIdx ] is marked as "used for
          // short-term reference", PicNumF( RefPicListX[ cIdx ] ) is
          //   the PicNum of the picture RefPicListX[ cIdx ].
          // - Otherwise (the picture RefPicListX[ cIdx ] is not marked as
          // "used for short-term reference"),
          //   PicNumF( RefPicListX[ cIdx ] ) is equal to MaxPicNum.
          if (picture &&
              (picture->referenceFlag & used_for_short_term_reference)) {
            return picture->PicNum;
          } else {
            return (int64_t)MaxPicNum;
          }
        };
        // Hint : the length of the list RefPicListX is temporarily made
        // one element longer than the length needed for the final list
        RefPicListX.resize((num_ref_idx_lX_active_minus1 + 1) + 1);
        for (uint32_t cIdx = num_ref_idx_lX_active_minus1 + 1; cIdx > refIdxLX;
             cIdx--) {
          RefPicListX[cIdx] = RefPicListX[cIdx - 1];
        }
        // short-term reference picture with PicNum equal to picNumLX
        RefPicListX[refIdxLX++] = findPictureByPicNum(picNumLX);
        uint32_t nIdx = refIdxLX;
        for (uint32_t cIdx = refIdxLX; cIdx <= num_ref_idx_lX_active_minus1 + 1;
             cIdx++) {
          if (PicNumF(RefPicListX[cIdx]) != picNumLX) {
            RefPicListX[nIdx++] = RefPicListX[cIdx];
          }
        }
        for (; nIdx <= num_ref_idx_lX_active_minus1 + 1; nIdx++) {
          RefPicListX[nIdx++] = nullptr;
        }
        // Hint :  After the execution of this procedure, only elements 0
        // through num_ref_idx_lX_active_minus1 of the list need to be
        // retained.
        RefPicListX.resize(num_ref_idx_lX_active_minus1 + 1);
      }
    }
    // 8.2.4.3.2 Modification process of reference picture lists for
    // long-term reference pictures
    else if (modification_of_pic_nums_idc == 2) {
      uint32_t long_term_pic_num =
          slice->rplm.modification_of_pic_nums_idcs_datas[index++]
              .long_term_pic_num;
      auto LongTermPicNumF = [&](uint32_t cIdx) -> uint32_t {
        for (auto picture : pictures) {
          if (picture->LongTermFrameIdx == cIdx) {
            if (picture->referenceFlag & used_for_long_term_reference) {
              return cIdx;
            }
          }
        }
        return (uint32_t)(2 * (curPicture->MaxLongTermFrameIdx + 1));
      };
      // Hint : the length of the list RefPicListX is temporarily made one
      // element longer than the length needed for the final list
      RefPicListX.resize((num_ref_idx_lX_active_minus1 + 1) + 1);
      for (uint32_t cIdx = num_ref_idx_lX_active_minus1 + 1; cIdx > refIdxLX;
           cIdx--) {
        RefPicListX[cIdx] = RefPicListX[cIdx - 1];
      }

      RefPicListX[refIdxLX++] = findPictureByLongTermPicNum(long_term_pic_num);
      uint32_t nIdx = refIdxLX;
      for (uint32_t cIdx = refIdxLX; cIdx <= num_ref_idx_lX_active_minus1 + 1;
           cIdx++) {
        if (LongTermPicNumF(RefPicListX[cIdx]->LongTermPicNum) !=
            long_term_pic_num) {
          RefPicListX[nIdx++] = RefPicListX[cIdx];
        }
      }
      for (; nIdx <= num_ref_idx_lX_active_minus1 + 1; nIdx++) {
        RefPicListX[nIdx++] = nullptr;
      }
      // Hint :  After the execution of this procedure, only elements 0
      // through num_ref_idx_lX_active_minus1 of the list need to be
      // retained.
      RefPicListX.resize(num_ref_idx_lX_active_minus1 + 1);
    } else if (modification_of_pic_nums_idc == 3) {
      break;
    }
  }
}

H264PicturePtr H264Decoder::findPictureByPicNum(int32_t pic_num) {
  H264PicturePtr pictRef = nullptr;
  for (const auto &picture : pictures) {
    if (picture->referenceFlag & used_for_short_term_reference) {
      if (pic_num == picture->PicNum) {
        pictRef = picture;
        break;
      }
    }
  }
  assert(pictRef);
  return pictRef;
}

H264PicturePtr
H264Decoder::findPictureByLongTermPicNum(int32_t long_term_pic_num) {
  H264PicturePtr pictRef = nullptr;
  for (const auto &picture : pictures) {
    if (picture->referenceFlag & used_for_short_term_reference) {
      if (long_term_pic_num == picture->LongTermPicNum) {
        pictRef = picture;
        break;
      }
    }
  }
  assert(pictRef);
  return pictRef;
}

void H264Decoder::decodeReferencePictureMarkingProcess() {
  // 非0表示参考帧,如I帧
  if (unit->RefIdc == 0) {
    return;
  }
  if (unit->nal == H264NAL::NAL_IDR) {
    if (slice->drpm.long_term_reference_flag == 0) {
      updateReferenceFlag(curPicture, used_for_short_term_reference, false);
      curPicture->MaxLongTermFrameIdx = no_long_term_frame_indices;
    } else if (slice->drpm.long_term_reference_flag == 1) {
      updateReferenceFlag(curPicture, used_for_long_term_reference, false);
      curPicture->LongTermFrameIdx = 0;
      curPicture->MaxLongTermFrameIdx = 0;
    }
  } else {
    // 像标记模式
    if (slice->drpm.adaptive_ref_pic_marking_mode_flag == 0) {
      /* Sliding window reference picture marking mode */
      // 解码器使用滑动窗口参考图像标记模式
      slidingWindowDecodedReferencePictureMarkingProcess();
    } else { /* Adaptive reference picture marking mode */
      // 表示解码器使用自适应参考图像标记模式
      adaptiveMemoryControlDecodedReferencePicutreMarkingPorcess();
    }
  }
  if (slice->slice_type != H264SliceType::MMP_H264_I_SLICE &&
      !(curPicture->referenceFlag & used_for_long_term_reference)) {
    updateReferenceFlag(curPicture, used_for_short_term_reference, false);
  }
}

void H264Decoder::updateReferenceFlag(H264PicturePtr picture, uint8_t flag,
                                      bool append) {
  if (append) {
    picture->referenceFlag |= flag;
  } else {
    picture->referenceFlag = flag;
  }
  if (flag == 2) {
    log(LogLevel::info, "updateReferenceFlag long frame");
  }
}

void H264Decoder::slidingWindowDecodedReferencePictureMarkingProcess() {
  if (curPicture->bottom_field_flag) {
    H264PicturePtr compPicture = findComplementaryPicture(curPicture);
    if (!compPicture) {
      return;
    }
    if (compPicture->referenceFlag & used_for_short_term_reference) {
      updateReferenceFlag(curPicture, used_for_short_term_reference);
    }
  } else {
    uint32_t numShortTerm = 0, numLongTerm = 0;
    for (auto &picture : pictures) {
      if (picture->referenceFlag & used_for_short_term_reference) {
        numShortTerm++;
      }
      if (picture->referenceFlag & used_for_long_term_reference) {
        numLongTerm++;
      }
    }
    if (numShortTerm + numLongTerm == std::max(1u, sps->max_num_ref_frames)) {
      H264PicturePtr minPict = nullptr;
      int64_t minFrameNumWrap = INT64_MAX;
      for (auto &picture : pictures) {
        if (picture->FrameNumWrap < minFrameNumWrap) {
          minFrameNumWrap = picture->FrameNumWrap;
          minPict = picture;
        }
      }
      updateReferenceFlag(minPict, unused_for_reference, false);
      if (minPict->field_pic_flag == 1) {
        assert(false);
      }
    }
  }
}

void H264Decoder::adaptiveMemoryControlDecodedReferencePicutreMarkingPorcess() {
  size_t index = 0;
  bool currentRefAssigned = false;
  for (const auto &memory_management_control_operation :
       slice->drpm.memory_management_control_operations) {
    switch (memory_management_control_operation) {
    case H264MmcoType::MMP_H264_MMCO_0: {
      break;
    }
    // See also : 8.2.5.4.1 Marking process of a short-term reference picture
    // as "unused for reference"
    case H264MmcoType::MMP_H264_MMCO_1: {
      uint32_t difference_of_pic_nums_minus1 =
          slice->drpm.memory_management_control_operations_datas[index++]
              .difference_of_pic_nums_minus1;
      int32_t picNumX = GetPicNumX(slice, difference_of_pic_nums_minus1);
      unMarkUsedForShortTermReference(picNumX);
      break;
    }
    // See also : 8.2.5.4.2 Marking process of a long-term reference picture
    // as "unused for reference"
    case H264MmcoType::MMP_H264_MMCO_2: /* unmark long term reference by
                                           LongTermPicNum */
    {
      uint32_t long_term_pic_num =
          slice->drpm.memory_management_control_operations_datas[index++]
              .long_term_pic_num;
      unMarkUsedForLongTermReference(long_term_pic_num);
      break;
    }
    // See also : 8.2.5.4.3 Assignment process of a LongTermFrameIdx to a
    // short-term reference picture
    case H264MmcoType::MMP_H264_MMCO_3: /* short term reference to long term
                                           reference */
    {
      uint32_t difference_of_pic_nums_minus1 =
          slice->drpm.memory_management_control_operations_datas[index++]
              .difference_of_pic_nums_minus1;
      uint32_t long_term_frame_idx =
          slice->drpm.memory_management_control_operations_datas[index++]
              .long_term_frame_idx;
      int32_t picNumX = GetPicNumX(slice, difference_of_pic_nums_minus1);
      unMarkUsedForReference(curPicture->long_term_frame_idx);
      markShortTermReferenceToLongTermReference(picNumX, long_term_frame_idx);
      break;
    }
    // See also : 8.2.5.4.4 Decoding process for MaxLongTermFrameIdx
    case H264MmcoType::MMP_H264_MMCO_4: /* set maximum long-frame index */
    {
      uint32_t max_long_term_frame_idx_plus1 =
          slice->drpm.memory_management_control_operations_datas[index++]
              .max_long_term_frame_idx_plus1;
      int64_t MaxLongTermFrameIdx = max_long_term_frame_idx_plus1 == 0
                                        ? no_long_term_frame_indices
                                        : max_long_term_frame_idx_plus1 - 1;
      for (auto picture : pictures) {
        if ((picture->referenceFlag & used_for_long_term_reference) &&
            (picture->LongTermFrameIdx > max_long_term_frame_idx_plus1 - 1)) {
          updateReferenceFlag(picture, unused_for_reference, false);
          picture->MaxLongTermFrameIdx = MaxLongTermFrameIdx;
        }
      }
      break;
    }
    // See also : 8.2.5.4.5 Marking process of all reference pictures as
    // "unused for reference" and setting
    //            MaxLongTermFrameIdx to "no long-term frame indices"
    case H264MmcoType::MMP_H264_MMCO_5: /* unmark all reference pictures */
    {
      for (auto picture : pictures) {
        picture->MaxLongTermFrameIdx = no_long_term_frame_indices;
        updateReferenceFlag(picture, unused_for_reference, false);
      }
      curPicture->has_memory_management_control_operation_5 = true;
      break;
    }
    // See also : 8.2.5.4.6 Process for assigning a long-term frame index to
    // the current picture
    case H264MmcoType::MMP_H264_MMCO_6: /* mark current picture long term */
    {
      uint32_t long_term_frame_idx =
          slice->drpm.memory_management_control_operations_datas[index++]
              .long_term_frame_idx;
      updateReferenceFlag(curPicture, used_for_long_term_reference, false);
      curPicture->LongTermFrameIdx = long_term_frame_idx;
      currentRefAssigned = true;
      break;
    }
    default:
      break;
    }
  }
  // See also FFmpeg 6.x : int ff_h264_execute_ref_pic_marking(H264Context *h)
  if (!currentRefAssigned) {
    bool found = false;
    for (auto &picture : pictures) {
      if (curPicture == picture) {
        found = true;
        break;
      }
    }
    if (!found) {
      updateReferenceFlag(curPicture, used_for_short_term_reference, false);
      pictures.push_back(curPicture);
    }
  }
}

void H264Decoder::unMarkUsedForShortTermReference(int32_t picNumX) {
  for (auto picture : pictures) {
    if ((picture->PicNum == picNumX) &&
        (picture->referenceFlag & used_for_short_term_reference)) {
      if (picture->field_pic_flag == 0) {
        updateReferenceFlag(picture, unused_for_reference, false);
      } else if (picture->field_pic_flag == 1) {
        // Hint : not support for now
        assert(false);
      }
      // Hint : 参考 FFmpeg 6.x 以及 openh264 , 此处应当只 umark 一个 short term
      // picture, 按照 DPB 的顺序
      //        同时 ISO 中也存在 `a short-term reference picture` 而非 `all
      //        short-term refernce pictures`
      break;
    }
  }
}

void H264Decoder::unMarkUsedForLongTermReference(int32_t long_term_pic_num) {
  for (auto picture : pictures) {
    if (picture->LongTermPicNum == long_term_pic_num) {
      if (picture->field_pic_flag == 0) {
        updateReferenceFlag(picture, unused_for_reference, false);
        H264PicturePtr compPicture = findComplementaryPicture(picture);
        if (compPicture) {
          updateReferenceFlag(compPicture, unused_for_reference, false);
        }
      } else if (picture->field_pic_flag == 1) {
        // Hint : not support for now
        assert(false);
      }
      // Hint : 参考 FFmpeg 6.x 以及 openh264 , 此处应当只 umark 一个 short term
      // picture, 按照 DPB 的顺序
      //        同时 ISO 中也存在 `a short-term reference picture` 而非 `all
      //        short-term refernce pictures`
      break;
    }
  }
}

void H264Decoder::unMarkUsedForReference(int32_t long_term_pic_num) {
  for (auto picture : pictures) {
    if (picture->LongTermPicNum == long_term_pic_num &&
        (picture->referenceFlag & used_for_long_term_reference)) {
      updateReferenceFlag(picture, unused_for_reference, false);
      H264PicturePtr compPicture = findComplementaryPicture(picture);
      if (compPicture) {
        updateReferenceFlag(compPicture, unused_for_reference, false);
      }
    }
  }
}

void H264Decoder::markShortTermReferenceToLongTermReference(
    int32_t picNumX, uint32_t long_term_frame_idx) {
  for (auto picture : pictures) {
    if (picture->field_pic_flag == 0) {
      if ((picture->referenceFlag & used_for_short_term_reference) &&
          picture->PicNum == picNumX) {
        H264PicturePtr compPicture = findComplementaryPicture(picture);
        if (compPicture) {
          updateReferenceFlag(compPicture, used_for_long_term_reference, false);
          compPicture->LongTermFrameIdx = long_term_frame_idx;
        }
        updateReferenceFlag(picture, used_for_long_term_reference, false);
        picture->LongTermFrameIdx = long_term_frame_idx;
      }
    } else if (picture->field_pic_flag == 1) {
      // Hint : not support for now
      assert(false);
    }
  }
}

H264PicturePtr H264Decoder::findComplementaryPicture(H264PicturePtr xpicture) {
  H264PicturePtr pictRef = nullptr;
  if (xpicture->field_pic_flag == 0) {
  } else if (xpicture->bottom_field_flag) {
    // curPicture second field
    for (auto &picture : pictures) {
      if (picture->PicNum == xpicture->PicNum - 1) {
        pictRef = picture;
        break;
      }
    }
  } else if (!xpicture->bottom_field_flag) {
    // curPicture first field
    for (auto &picture : pictures) {
      if (picture->PicNum == xpicture->PicNum + 1) {
        pictRef = picture;
        break;
      }
    }
  }
  return pictRef;
}

}