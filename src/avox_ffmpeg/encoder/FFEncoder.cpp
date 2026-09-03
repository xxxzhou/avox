#include "FFEncoder.hpp"

namespace avox {

FFEncoder::FFEncoder() {}

FFEncoder::~FFEncoder() {}

DecodeResult FFEncoder::onFrame(PackType type) { return DecodeResult::success; }

}