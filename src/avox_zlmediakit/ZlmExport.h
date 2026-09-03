#pragma once

#include "avox/AvoxCodec.h"

namespace avox {

extern "C" {
AVOX_EXPORT bool checkOnvif(const char* url, ATrackDesc* trackDesc);
// 保存腾讯云 API 凭证(HttpTranslator::loadConfig 运行期读取)
AVOX_EXPORT void saveTencentApi(const char* secretId, const char* secretKey);
}

}
