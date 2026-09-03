#include "SherpaHelper.hpp"

#include "SherpaAudioStt.hpp"
#include "SherpaAudioTts.hpp"

namespace avox {

AudioStt* createAudioSttSherpa() {
  return new SherpaAudioStt();
}

AudioTts* createAudioTtsSherpa() {
  return new SherpaAudioTts();
}

}
