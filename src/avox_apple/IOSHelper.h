#pragma once

#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"

namespace avox {

void logApple(const char *time, const char *level, const char *msg);

const char *getShaderPath(const char *spvPath);
const char *getFontPath(const char *fontName);
const char *getImagePath(const char *imageName);
const char *getModelPath(const char *imageName);

#ifdef __OBJC__
@class NSString;

// 从 avox.bundle 查找资源完整路径
const char *getBundlePath(NSString *bundleName, NSString *resourceName);
#endif

float getIosDeviceSystemVersion();

// 设置音频路由：bSpeaker 为 true 走扬声器，false 走听筒
void setIosAudioRoute(bool bSpeaker);

}
