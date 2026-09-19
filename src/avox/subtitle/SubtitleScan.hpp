#pragma once

#include <string>
#include <vector>

#include "../AvoxCodec.h"

namespace avox {

// 外挂字幕候选(扫描产物, STL 形态内部用; 对外透出见 AvoxPlayer.h 的 POD 版)
struct SubtitleCandidateInfo {
  std::string path = "";  // UTF-8 绝对路径
  SCodecId codecId = SCodecId::ass;
  std::string lang = "";  // 语言标记 hint(小写原名: zh/chs/cht/gb/big5/eng...), 空=未标
  bool gbkHint = false;   // 带 .gbk 编码后缀(内容大概率 GBK)
};

// 同目录「同主名」字幕候选枚举(a01-T5): 主名 = 视频路径去扩展名; 候选 = 同目录
// 「去扩展名后等于主名, 或以 主名+分隔符(.-_) 开头」且扩展名 .srt/.ass/.ssa 的
// 文件。排序: 完全同名 > 带标记段, srt > ass, 同级按路径。非本地路径(带 ://)
// 或目录不可读返回空。纯文件系统查询: 不打开字幕内容, 加载仍走 loadSubtitle
int32_t listSubtitleCandidates(const std::string& videoUrl,
                               std::vector<SubtitleCandidateInfo>* out);

}
