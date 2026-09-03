#pragma once
// Extension point. Capture sources, broadcast IO and AI modules all load as
// plugins; the media core never depends on a plugin direction.
#include <string_view>

namespace avox {

struct PluginInfo {
  std::string_view name;
  std::string_view version;
};

class IPlugin {
 public:
  virtual ~IPlugin() = default;
  virtual PluginInfo info() const = 0;
  virtual bool load() = 0;
  virtual void unload() = 0;
};

}  // namespace avox
