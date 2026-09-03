#pragma once

#include <assert.h>

#include <memory>
#include <string>
#include <vector>

#include "../AvoxLayer.h"

namespace avox {

#define AVOX_LAYER_GETNAME(CLASS) \
 public:                         \
  virtual const char* getName() override { return #CLASS; }

template <typename T>
class IParamet {
 public:
  IParamet() {};
  virtual ~IParamet() {};

 protected:
  T oldParamet = {};
  T paramet = {};

 protected:
  virtual void onUpdateParamet() = 0;

 public:
  void updateParamet(const T& t) {
    oldParamet = paramet;
    paramet = t;
    onUpdateParamet();
  }
  T getParamet() { return paramet; }
};

using IBlendLayer = IParamet<BlendParamet>;
using IYUVLayer = IParamet<YuvType>;
using IMapChannelLayer = IParamet<MapChannelParamet>;
using IFlipLayer = IParamet<FlipParamet>;
using ITransposeLayer = IParamet<TransposeParamet>;
using IReSizeLayer = IParamet<ReSizeParamet>;
using IGammaLayer = IParamet<float>;

}