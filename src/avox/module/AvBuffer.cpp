#include "AvBuffer.hpp"

namespace avox {

AvBuffer::AvBuffer(/* args */) {}

AvBuffer::~AvBuffer() { buffer.clear(); }

void AvBuffer::setSize(int32_t itemCount, int32_t itemSize) {
  icount = itemCount;
  isize = itemSize;
  if (icount <= 0 || isize <= 0) {
    return;
  }
  if (buffer.size() != itemCount * itemSize) {
    buffer.resize(itemCount * itemSize);
  }
}

int32_t AvBuffer::size() { return buffer.size(); }

uint8_t* AvBuffer::pointer() { return buffer.data(); }

int32_t AvBuffer::itemCount() { return icount; }

int32_t AvBuffer::itemSize() { return isize; }

void AvBuffer::clear() { buffer.clear(); }

}