#pragma once
#include <stdint.h>
#include "libavutil/attributes.h"
#include "libavutil/log.h"
void avpriv_request_sample(void *avc, const char *msg, ...) av_printf_format(2, 3);
#ifndef SUINT
#define SUINT unsigned
#endif
#define SUINT32 uint32_t
