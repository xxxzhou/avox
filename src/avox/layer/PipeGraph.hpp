#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <stack>
#include <vector>

#include "../AvoxVideo.h"
#include "PipeNode.hpp"
#include "VLayer.hpp"

namespace avox {

using NodeLinePtr = std::shared_ptr<NodeLine>;

inline bool findLineNode(NodeLinePtr line,
                         const std::vector<NodeLinePtr>& lines) {
  if (!line) {
    return false;
  }
  for (const auto& element : lines) {
    if (element && *element == *line) {
      return true;
    }
  }
  return false;
}

// 有向无环图
// T表示具体实现,如vulkan图像处理管线里为VkLayer
// layer的创建和释放由PipeGraph负责
// 外部接口创建layer时，需要指定PipeGraph
template <typename T>
class PipeGraph : public IPipeGraph {
 public:
  PipeGraph() {};
  virtual ~PipeGraph() {};

  // 使用 static_assert 强制 T 有 SlotType 类型
  static_assert(std::is_same<typename T::SlotType, typename T::SlotType>::value,
                "T must have a type named SlotType");
  using S = typename T::SlotType;

 protected:
  // 节点
  std::vector<NodePtr<T>> nodes;
  // 连接线
  std::vector<NodeLinePtr> lines;
  // 有效的连接线
  std::vector<NodeLinePtr> validLines;

  // 节点处理锁，因为节点可能是群集的，可能有回环
  std::recursive_mutex nodeMtx;
  // 当由节点变化引起的重置
  std::atomic<bool> bReset = false;
  // 图表的执行顺序
  std::vector<int32_t> nodeExcs;

  bool bInit = false;

 public:
  // template <typename TP, typename... Args>
  // TNodePtr<TP, T> addNode(Args &&...args) {
  //   return addNode<TP, TP>(std::forward<Args>(args)...);
  // }
  template <typename TP, typename... Args>
  TNodePtr<TP, T> addNode(Args&&... args) {
    static_assert(std::is_base_of<T, TP>::value, "TP must be a subclass of T");
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    // 使用可变参数创建 T 类型的对象
    auto layer = std::make_unique<TP>(std::forward<Args>(args)...);
    layer->setPipeGraph(this, nodes.size());
    auto node = std::make_shared<TPipeNode<TP, T>>(layer.get());
    nodes.push_back(node);
    layer->attach();
    bReset.store(true);
    // 因为 node 已经接管了 layer 的所有权
    layer.release();
    return node;
  }

  NodePtr<T> getTNode(int32_t index) {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    assert(index < nodes.size());
    return nodes[index];
  }

  bool addLine(NodeSlot from, NodeSlot to) {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    NodeLinePtr line = std::make_shared<NodeLine>();
    line->from = from;
    line->to = to;
    // 检查slot有效性
    LOG_RETURN_FLASE(from.slot < nodes[from.index]->outSlotCount(),
                     nodes[from.index]->getName(),
                     " out node index out of range, out size:",
                     nodes[from.index]->outSlotCount(), " index:", from.slot);
    LOG_RETURN_FLASE(
        to.slot < nodes[to.index]->inSlotCount(), nodes[from.index]->getName(),
        " in node index out of range, in size:", nodes[to.index]->inSlotCount(),
        " index:", to.slot);
    LOG_RETURN_FLASE(from.index != to.index, nodes[from.index]->getName(),
                     " connect self");
    LOG_RETURN_FLASE(line->valid(), nodes[from.index]->getName(),
                     " line invalid");
    bool bHave = findLineNode(line, lines);
    LOG_RETURN_FLASE(!bHave, nodes[from.index]->getName(),
                     " line already exist");
    lines.push_back(line);
    bReset.store(true);
    return true;
  }
  bool addLine(int32_t fromIndex, int32_t fromSlot, int32_t toIndex,
               int32_t toSlot) {
    return addLine({fromIndex, fromSlot}, {toIndex, toSlot});
  }

 private:
  NodePtr<T> addLineX(IPipeNode* a, IPipeNode* b, int32_t aslot = 0,
                      int32_t bslot = 0) {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    int32_t formIndex = a->getNodeIndex();
    PipeNode<T>* bnode = dynamic_cast<PipeNode<T>*>(b);
    assert(bnode);
    // b不是群集
    if (!b->bGroup()) {
      // 如果是输入层,不能使用addLine的方法给输入层加线,会构成回环
      if (bnode->bInputNode()) {
        return getTNode(bnode->getNodeIndex());
      }
      assert(aslot < a->outSlotCount());
      int32_t toIndex = bnode->getNodeIndex();
      assert(bslot < bnode->inSlotCount());
      addLine({formIndex, aslot}, {toIndex, bslot});
      return getTNode(bnode->getNodeIndex());
    } else {
      IGroup* nGroup = dynamic_cast<IGroup*>(bnode->getLayer());
      const auto& startNodes = nGroup->getStartNodes(bslot);
      // 已经指定setStartNode了，外部节点连接本身，本身当做一个群集时
      if (startNodes.size() > 0) {
        for (const auto& startNode : startNodes) {
          addLine({formIndex, aslot}, startNode);
        }
      } else {
        // 还没指定setStartNode时，内部节点连接本身时调用
        addLine({formIndex, aslot}, {bnode->getNodeIndex(), bslot});
      }
      return getTNode(nGroup->getEndIndex());
    }
  }

 public:
  NodePtr<T> addLine(NodePtr<T> a, NodePtr<T> b, int32_t aslot = 0,
                     int32_t bslot = 0) {
    NodePtr<T> ptr = addLineX(a.get(), b.get(), aslot, bslot);
    return ptr;
  }

  // IPipeGraph实现
 public:
  virtual IPipeNode* getNode(int32_t index) override {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    if (index >= nodes.size()) {
      return nullptr;
    }
    return nodes[index].get();
  }
  virtual IPipeNode* addLine(IPipeNode* a, IPipeNode* b, int32_t aslot = 0,
                             int32_t bslot = 0) override {
    NodePtr<T> ptr = addLineX(a, b, aslot, bslot);
    return ptr.get();
  };

  virtual void clearLines() override {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    lines.clear();
    validLines.clear();
    nodeExcs.clear();
    bReset.store(true);
  }
  virtual void clear() override {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    clearLines();
    // 置空
    for (auto& node : nodes) {
      node->getLayer()->detach();
    }
    nodes.clear();
  }

  virtual void run() override {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    if (bReset.load()) {
      onReset();
      bInit = resetGraph();
      if (bInit) {
        log(LogLevel::info, "Pipegraph reset success");
      } else {
        log(LogLevel::warn, "Pipegraph reset failed");
      }
      bReset.store(false);
    }
    if (bInit) {
      onRun();
    }
  }
  virtual void reset() override {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    bReset.store(true);
  }

 protected:
  void validNode() {
    // 节点的输出线
    std::vector<std::vector<NodeLinePtr>> formLines(nodes.size());
    validLines.clear();
    for (auto& line : lines) {
      if (line->from.index >= formLines.size()) {
        return;
      }
      formLines[line->from.index].push_back(line);
    }
    // 检查有效链路,保存到validLines
    for (auto& node : nodes) {
      // 从可用的输入节点向下深度优先搜索
      if (node->bInputNode() && node->visible()) {
        // stack深度优先搜索
        std::stack<int32_t> inputNodes;
        inputNodes.push(node->getNodeIndex());
        int32_t nodeIndex = node->getNodeIndex();
        // 查找所有有效链路
        while (!inputNodes.empty()) {
          int32_t currentIndex = inputNodes.top();
          inputNodes.pop();
          // 当前节点向下连接
          auto& nextLines = formLines[currentIndex];
          // 当前节点不可用,继续用上一个可用节点
          if (nodes[currentIndex]->visible()) {
            nodeIndex = currentIndex;
          }
          // 验证这些连接是否有效
          for (auto line : nextLines) {
            // 如果当前节点不可用,这条线就不可用了
            int32_t toNode = line->to.index;
            // 当前连接节点不可见的话,尝试自动去掉当前节点链接下一节点
            if (!nodes[toNode]->visible()) {
              inputNodes.push(line->to.index);
              continue;
            }
            NodeLinePtr nline(new NodeLine());
            nline->from = {nodeIndex, line->from.slot};
            nline->to = {toNode, line->to.slot};
            // 正常情况,检查是否已经搜索过.
            if (!findLineNode(nline, validLines)) {
              validLines.push_back(nline);
              inputNodes.push(toNode);
            }
          }
        }
      }
    }
  }

  bool resetGraph() {
#if AVOX_DEBUG
    for (auto node : nodes) {
      LOGFLF(LogLevel::warn, node->getName());
    }
#endif
    // 1. 得到节点的enable/visable验证过的lines
    validNode();
    // 2. 得到有效节点的前置节点  如:2[1] 3[2] 4[1] 5[3,4]
    // (2需要1),(5需要3,4)
    std::vector<std::vector<int32_t>> reqNodes(nodes.size());
    // 需要确定顺序的节点
    std::queue<int32_t> tempQueue;
    std::vector<int32_t> checkRepeat;
    for (auto& line : validLines) {
      reqNodes[line->to.index].push_back(line->from.index);
      if (std::find(checkRepeat.begin(), checkRepeat.end(), line->to.index) ==
          checkRepeat.end()) {
        checkRepeat.push_back(line->to.index);
        tempQueue.push(line->to.index);
      }
      // 填充layer的,此时经过enable/visable过滤后,每个输入节点应该是一一对应的
      nodes[line->to.index]->addInLayer(line->to.slot, line->from.index,
                                        line->from.slot);
      nodes[line->from.index]->addOutLayer(line->from.slot, line->to.index,
                                           line->to.slot);
    }
    nodeExcs.clear();
    for (int32_t i = 0; i < nodes.size(); i++) {
      const auto& reqNode = reqNodes[i];
      if (nodes[i]->bInputNode() && nodes[i]->visible()) {
        nodeExcs.push_back(i);
      }
    }
    // 3. 重新构建有序无环图的执行顺序
    while (!tempQueue.empty()) {
      int32_t tnode = tempQueue.front();
      tempQueue.pop();
      auto& reqNode = reqNodes[tnode];
      bool bfind = true;
      // 如果当前的前置节点已经全部添加到nodeExcs中,则可以放入执行列表中
      for (auto& rnode : reqNode) {
        if (std::find(nodeExcs.begin(), nodeExcs.end(), rnode) ==
            nodeExcs.end()) {
          bfind = false;
          break;
        }
      }
      if (bfind) {
        nodeExcs.push_back(tnode);
      } else {
        tempQueue.push(tnode);
      }
    }
    if (!onValidNodes()) {
      return false;
    }
    onInitLayers();
    for (auto index : nodeExcs) {
      nodes[index]->getLayer()->initBuffer();
    }
    onInitBuffers();
    return true;
  }

 protected:
  // 当开始重置管线时
  virtual void onReset() {};
  // 子类验证管线是否有效
  virtual bool onValidNodes() { return false; }
  // 层开始初始化
  virtual void onInitLayers() {}
  // 层分配buffer
  virtual void onInitBuffers() {}
  // 开始执行
  virtual void onRun() {}

 public:
  void getInSlot(NodeSlot ns, S& s) {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    LOGASSERT(ns.index < nodes.size(), "out of range")
    nodes[ns.index]->getLayer()->getInSlot(ns.slot, s);
  }
  void getOutSlot(NodeSlot ns, S& s) {
    std::lock_guard<std::recursive_mutex> lock(nodeMtx);
    LOGASSERT(ns.index < nodes.size(), "out of range")
    nodes[ns.index]->getLayer()->getOutSlot(ns.slot, s);
  }
};

// 基本的图像处理管线
template <typename T>
class VPipeGraph : public PipeGraph<T> {
 public:
  VPipeGraph() {};
  virtual ~VPipeGraph() {};

 protected:
  GpuType gpu = GpuType::noGpu;

 protected:
  virtual bool onValidNodes() override {
    // 4. 检查输入层大小是否确认，移除无效且无关联的输入节点
    std::vector<int32_t> removeIndices;
    for (auto& index : this->nodeExcs) {
      T* layer = this->nodes[index]->getLayer();
      if (layer->bInput) {
        // 检查输入层是否已经有效设置输入格式
        if (layer->inFormats.size() > 0) {
          ImageFormat imageFormat = layer->inFormats[0];
          if (imageFormat.width == 0 || imageFormat.height == 0) {
            // 检查validLines中是否有以该节点为起点的连接
            bool hasConnection = false;
            for (const auto& line : this->validLines) {
              if (line->from.index == index) {
                hasConnection = true;
                break;
              }
            }
            if (!hasConnection) {
              LOGFLF(LogLevel::info, "the index:", index,
                     " inputlayer imageformat width/height is 0 and no "
                     "connection, remove it");
              removeIndices.push_back(index);
              continue;
            }
            LOGFLF(LogLevel::warn, "the index:", index,
                   " inputlayer imageformat width/height is 0");
            return false;
          }
        }
      }
    }
    // 从nodeExcs中移除无效节点
    for (auto removeIndex : removeIndices) {
      auto it =
          std::find(this->nodeExcs.begin(), this->nodeExcs.end(), removeIndex);
      if (it != this->nodeExcs.end()) {
        this->nodeExcs.erase(it);
      }
    }
    // 5. onInitLayer,节点得到所需大小,输入图像大小沿着有序无环图开始确定
    for (auto index : this->nodeExcs) {
      if (!this->nodes[index]->vaildInLayers()) {
        LOGFLF(LogLevel::warn, this->nodes[index]->getName(), " vaild error ");
        return false;
      }
      this->nodes[index]->getLayer()->template initLayer<T>(this);
    }
    // 6. 检查节点连接的ImageType是否符合
    for (auto& index : this->nodeExcs) {
      // 输入层不检查ImageType
      if (this->nodes[index]->bInputNode()) {
        continue;
      }
      int size = this->nodes[index]->inNodes.size();
      for (int i = 0; i < size; i++) {
        const auto& fromNode = this->nodes[index]->inNodes[i];
        T* srcLayer = this->nodes[fromNode.index]->getLayer();
        T* dstLayer = this->nodes[index]->getLayer();
        ImageType srcImageType = srcLayer->outFormats[fromNode.slot].imageType;
        ImageType dstImageType = dstLayer->inFormats[i].imageType;
        LOGFLF(LogLevel::info, "graph node from ", srcLayer->getDesc(), "-",
               fromNode.slot, "(", getImageTypeStr(srcImageType), ")", " to ",
               this->nodes[index]->getName(), "-", i, "(",
               getImageTypeStr(dstImageType), ")");

        if (srcImageType != dstImageType) {
          LOGFLF(LogLevel::warn, "graph node from ", srcLayer->getDesc(), "-",
                 fromNode.slot, "(", getImageTypeStr(srcImageType), ")",
                 " not match ", this->nodes[index]->getName(), "-", i, "(",
                 getImageTypeStr(dstImageType), ")");
          return false;
        }
      }
    }
    return true;
  }
};

// 用来编译实例化查错
class VBPipeGraph : public VPipeGraph<VLayer> {
 public:
  VBPipeGraph() {};
  virtual ~VBPipeGraph() {};
};

}
