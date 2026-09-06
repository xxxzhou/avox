#pragma once

#include "avox/video/Window.hpp"
#include <QuartzCore/QuartzCore.h>

namespace avox {

class MetalWindow : public Window {
public:
  MetalWindow();
  virtual ~MetalWindow();

protected:
  virtual void onChangeSize() override;
};

}
