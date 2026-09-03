#pragma once

#include <vector>

#include "../AvoxVideo.h"
#include "../module/LogHelper.hpp"
#include "PipeNode.hpp"

namespace avox {

// 基本的图像处理层
class VLayer : public ILayer<ImageFormat> {
 public:
  VLayer() : VLayer(1, 1) {}
  VLayer(int32_t inSize, int32_t outSize);
  virtual ~VLayer();

  template <typename T>
  friend class PipeGraph;
  template <typename T>
  friend class VPipeGraph;

 protected:
  GpuType gpu = GpuType::noGpu;
  IPipeGraph* pipeGraph = nullptr;
  std::string layerName = "";

  // 定义当前层需要的输入数量
  int32_t inCount = 1;
  // 定义当前层需要的输出数量
  int32_t outCount = 1;

  // 是否输入层
  bool bInput = false;
  // 是否输出层
  bool bOutput = false;

  // 每个层的imagetype对应shader里类型,连接时需要检测
  std::vector<ImageFormat> inFormats;
  // 每个层的imagetype对应shader里类型,连接时需要检测
  std::vector<ImageFormat> outFormats;
  int32_t graphIndex = -1;

 public:
  // 已经构建执行图，这时已经知道了输入层的inputFormats大小
  // 可以根据当前层的需求,设定对应outFormats
  template <typename T>
  void initLayer(PipeGraph<T>* graph) {
    int32_t size = inFormats.size();
    // 拿到上层的长宽
    if (!bInput) {
      for (int32_t i = 0; i < size; i++) {
        // 得到输入节点的输出格式
        NodeSlot inSlot = graph->getTNode(graphIndex)->inNodes[i];
        ImageFormat tempFormat = {};
        graph->getOutSlot(inSlot, tempFormat);
        inFormats[i].width = tempFormat.width;
        inFormats[i].height = tempFormat.height;
        // imageType由每层自己决定
        if (bOutput) {
          inFormats[i].imageType = tempFormat.imageType;
        }
      }
    }
    // 默认所有输出长宽为第一个输入
    if (inFormats.size() > 0) {
      for (auto& outFormat : outFormats) {
        outFormat.width = inFormats[0].width;
        outFormat.height = inFormats[0].height;
        if (bOutput) {
          outFormat.imageType = inFormats[0].imageType;
        }
      }
    }
    // 如果每层的outputFormat需要更新,请在如下函数单独处理
    onInitLayer();
  }
  void initBuffer();
  void resetGraph();

  const char* getDesc() {
    if (layerName.empty()) {
      string_format(layerName, "(", getNodeIndex(), ")", getName());
    }
    return layerName.c_str();
  }

  // INode实现
 public:
  virtual IPipeGraph* getPipeGraph() override { return pipeGraph; }
  virtual int32_t getNodeIndex() override { return graphIndex; }
  virtual int32_t inSlotCount() override { return inCount; }
  virtual int32_t outSlotCount() override { return outCount; }

  virtual bool bInputNode() override { return bInput; }
  virtual bool bOutputNode() override { return bOutput; }

  // ILayer实现
 public:
  AVOX_LAYER_GETNAME(VLayer)
  virtual void setPipeGraph(IPipeGraph* graph, int32_t nodeIndex) override {
    pipeGraph = graph;
    graphIndex = nodeIndex;
  }
  virtual void attach() override;
  virtual void detach() override;
  virtual void getInSlot(int32_t slot, ImageFormat& slotType) override {
    LOGASSERT(slot < inCount, "slot out of range");
    slotType = inFormats[slot];
  }
  virtual void getOutSlot(int32_t slot, ImageFormat& slotType) override {
    LOGASSERT(slot < outCount, "slot out of range");
    slotType = outFormats[slot];
  }
  virtual void setInSlot(int32_t slot, const ImageFormat& slotType) override {
    LOGASSERT(slot < inCount, "slot out of range");
    inFormats[slot] = slotType;
  }
  virtual void setOutSlot(int32_t slot, const ImageFormat& slotType) override {
    LOGASSERT(slot < outCount, "slot out of range");
    outFormats[slot] = slotType;
  }

 protected:
  // 添加进pipeGraph时调用
  virtual void onInit() {};
  // 在initLayer知道输入层的大小,根据当前层的需求,设定对应outFormats
  // 并可分配线程组的大小了
  virtual void onInitLayer() {}
  // 根据inputFormats初始化buffer
  virtual void onInitBuffer() {}
  // 更新参数,子类会有updateParamet(T t)保存参数,等到运行前提交执行
  virtual void onUpdateParamet() {};
  virtual bool onFrame() = 0;
  // 解除与pipeGraph的绑定,释放资源
  virtual void onUnInit() {}
};

}