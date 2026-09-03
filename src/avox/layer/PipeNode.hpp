#pragma once

#include "../AvoxLayer.h"
#include "AvoxNode.hpp"

namespace avox {

// S为Slot需要匹配的类型(如图像就是ImageFormat)
// 可以填充到PipeNode的接口要求
template <typename S>
class ILayer : public INode {
 public:
  ILayer() = default;
  virtual ~ILayer() = default;

  using SlotType = S;

 public:
  // 层名
  virtual const char* getName() = 0;
  // 输入图与节点索引
  virtual void setPipeGraph(IPipeGraph* graph, int32_t nodeIndex) = 0;
  // 附加到执行图
  virtual void attach() = 0;
  // 从执行图中移除
  virtual void detach() = 0;
  // 获取输入插槽数据
  virtual void getInSlot(int32_t slot, S& slotType) = 0;
  // 获取输出插槽数据
  virtual void getOutSlot(int32_t slot, S& slotType) = 0;
  // 设置输入插槽数据
  virtual void setInSlot(int32_t slot, const S& slotType) = 0;
  // 设置输出插槽数据
  virtual void setOutSlot(int32_t slot, const S& slotType) = 0;
  // 如果是组合节点，可能也需要对组合的节点进行设置
  virtual void onVisible(bool visible) {};
};

// 管线子图的连接信息,本身可以当做处理节点,也可以不是
// 组不参与数据流转,只定义外部与内部节点的映射关系
// startNodes: 外部输入槽 → 内部哪些节点接收该输入
// endIndex:   内部哪个节点的输出作为组的输出
// 经实践,在ILayer上实现最方便
// 自身也可当做节点的,比如自身也有glslPath的,如VkGuidedLayer
class IGroup {
 public:
  IGroup() { startNodes.resize(1); };
  virtual ~IGroup() = default;

 protected:
  // startNodes[外部输入槽] = 该槽对应的内部接收节点列表
  // 一个外部输入可以扇出到多个内部节点(如输入连接同时送入内部多节点)
  std::vector<std::vector<NodeSlot>> startNodes;
  // 组的输出取自哪个内部节点(该节点的输出纹理即下游读到的数据)
  // 如果不设置,自身就是结束节点,需要自身有逻辑处理(如glslPath)
  int32_t endIndex = -1;

 public:
  // 如何组合节点的具体实现
  void InitNode() { onInitNode(); }
  int32_t getEndIndex() { return endIndex; }

 protected:
  virtual void onInitNode() {}

 public:
  void setInCount(int32_t count) { startNodes.resize(count); }
  const std::vector<NodeSlot>& getStartNodes(int32_t index) {
    assert(index < startNodes.size());
    return startNodes[index];
  }

  void setStartNode(IPipeNode* node, int32_t index = 0, int32_t nodeSite = 0) {
    assert(index < startNodes.size());
    IGroup* nGroup = dynamic_cast<IGroup*>(node->getContent());
    // node就是自身或者node不是群集节点,则直接添加
    if (nGroup == this || !node->bGroup()) {
      startNodes[index].push_back({node->getNodeIndex(), nodeSite});
      return;
    }
    // 如果node也是群集节点,则需要递归
    const auto& startNodes = nGroup->getStartNodes(nodeSite);
    if (startNodes.size() > 0) {
      for (const auto& startNode : startNodes) {
        IPipeNode* cnode = node->getPipeGraph()->getNode(startNode.index);
        setStartNode(cnode, index, startNode.slot);
      }
    }
  }
  void setEndNode(IPipeNode* node) { endIndex = node->getNodeIndex(); }
};

template <typename T>
class PipeGraph;

// T对应ILayer具体实现
template <typename T>
class PipeNode : public IPipeNode {
 public:
  PipeNode(T* layer_) {
    static_assert(std::is_base_of<INode, T>::value,
                  "T must be subclass of INode");
    layer = std::shared_ptr<T>(layer_);
    inNodes.resize(layer->inSlotCount());
    outNodes.resize(layer->outSlotCount());
    graph = static_cast<PipeGraph<T>*>(layer->getPipeGraph());
  };
  virtual ~PipeNode() {};

 protected:
  std::shared_ptr<T> layer = nullptr;
  PipeGraph<T>* graph = nullptr;
  // 当前节点是否使用
  bool bVisible = true;

 public:
  T* getLayer() {
    // 当调用这个方法时，期望是有layer的
    assert(layer);
    return layer.get();
  }
  operator T*() {
    assert(layer);
    return layer.get();
  }

  T* operator->() {
    assert(layer);
    return layer.get();
  }

  template <typename TT>
  operator TT*() const {
    static_assert(std::is_base_of<TT, T>::value, "TT must be a subclass of T");
    return layer.get();
  }

  PipeGraph<T>* getGraph() { return graph; }

 public:
  // 每个节点插槽对应一个输入
  std::vector<NodeSlot> inNodes;
  // 每个节点插槽可以有多个输出
  std::vector<std::vector<NodeSlot>> outNodes;

 public:
  bool vaildInLayers() {
    // 输入层没有inLayers
    if (bInputNode()) {
      return true;
    }
    for (const auto& layer : inNodes) {
      if (layer.index < 0 || layer.slot < 0) {
        return false;
      }
    }
    return true;
  }
  const char* getName() { return layer->getDesc(); }
  void addInLayer(int32_t slot, int32_t index, int32_t slotIndex) {
    assert(slot < inSlotCount());
    inNodes[slot] = {index, slotIndex};
  }
  void addOutLayer(int32_t slot, int32_t index, int32_t slotIndex) {
    assert(slot < outSlotCount());
    outNodes[slot].push_back({index, slotIndex});
  }
  bool visible() { return bVisible; }
  void setVisible(bool visible) {
    if (bVisible == visible) {
      return;
    }
    bVisible = visible;
    if (layer) {
      layer->onVisible(visible);
    }
    graph->reset();
  }

  // INode
 public:
  virtual IPipeGraph* getPipeGraph() override { return layer->getPipeGraph(); }
  virtual int32_t getNodeIndex() override { return layer->getNodeIndex(); }
  virtual int32_t inSlotCount() override { return layer->inSlotCount(); }
  virtual int32_t outSlotCount() override { return layer->outSlotCount(); }

  virtual bool bInputNode() override { return layer->bInputNode(); }
  virtual bool bOutputNode() override { return layer->bOutputNode(); }
  virtual bool bGroup() override {
    IGroup* groupLayer = dynamic_cast<IGroup*>(layer.get());
    return groupLayer != nullptr;
  }
  virtual INode* getContent() { return layer.get(); }

  // IPipeNode
 public:
  virtual IPipeNode* addLine(IPipeNode* b, int32_t asite = 0,
                             int32_t bsite = 0) override {
    return graph->addLine(this, b, asite, bsite);
  }

 public:
  std::shared_ptr<PipeNode<T>> addLine(std::shared_ptr<PipeNode<T>> b,
                                       int32_t asite = 0, int32_t bsite = 0) {
    return graph->addLine(graph->getTNode(getNodeIndex()), b, asite, bsite);
  }
};

// L是要返回的接口,可以是IVInputLayer/IParamet<YUVParamet>这些
// TP是具体实现，必需传入TP,才能使用static_assert/static_cast
// T对应ILayer具体实现
template <typename TP, typename T>
class TPipeNode : public PipeNode<T> {
 public:
  TPipeNode(TP* layer_) : PipeNode<T>(layer_) {
    static_assert(std::is_base_of<T, TP>::value, "TP must be a subclass of T");
    nlayer = layer_;
  }
  virtual ~TPipeNode() {};

 protected:
  TP* nlayer = nullptr;

 public:
  TP* get() {
    // 当调用这个方法时，期望是有layer的
    assert(nlayer);
    return nlayer;
  }

  TP* operator->() {
    assert(nlayer);
    return nlayer;
  }
};

template <typename T>
using NodePtr = std::shared_ptr<PipeNode<T>>;
template <typename TP, typename T>
using TNodePtr = std::shared_ptr<TPipeNode<TP, T>>;

// 提供更加方便的操作
template <typename T>
class Group : public IGroup {
 public:
  Group() = default;
  virtual ~Group() = default;

 public:
  // 外部链接当前群集时,指定需要链接群集内的哪个节点
  // 有些群集本身也有实现，内部节点链接链接时，是本身的NodeIndex
  // 因此这个方法最好是在所有内部节点信息都设置好之后调用
  void setStartNode(std::shared_ptr<PipeNode<T>> node, int32_t index = 0,
                    int32_t nodeSite = 0) {
    IGroup::setStartNode(node.get(), index, nodeSite);
  }
  // 外部链接当前群集时,指定输出链接群集内的哪个节点
  void setEndNode(std::shared_ptr<PipeNode<T>> node) {
    IGroup::setEndNode(node.get());
  }
};

}