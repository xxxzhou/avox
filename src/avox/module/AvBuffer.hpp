#pragma once

#include <vector>
#include <memory>
#include "../AvoxBuffer.h"
namespace avox {

// 用来保存数据的缓冲区
class AvBuffer : public IAvBuffer {
 private:
  /* data */
  std::vector<uint8_t> buffer;
  int32_t icount = 0;
  int32_t isize = 1;

 public:
  AvBuffer(/* args */);
  virtual ~AvBuffer();

 public:
  virtual void setSize(int32_t itemCount, int32_t itemSize = 1) override;
  virtual int32_t size() override;
  virtual uint8_t* pointer() override;
  virtual int32_t itemCount() override;
  virtual int32_t itemSize() override;
  virtual void clear() override;
};

// 一般保存在队列中，关系用shared_ptr比较好
typedef std::shared_ptr<AvBuffer> AvBufferPtr;

}