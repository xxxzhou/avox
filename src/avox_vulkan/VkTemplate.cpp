#include "VkTemplate.hpp"

#include "layer/VkInputLayer.hpp"
#include "layer/VkOutputLayer.hpp"
#include "layer/VkPipeGraph.hpp"
#include "layer/VkYUV2RGBALayer.hpp"
#include "vulkan/VkWindow.hpp"
#include "vulkan/VkVideoRender.hpp"
#include "avox/video/SurfaceRenderVk.hpp"

namespace avox {

// #define AVOX_GET_VK_NODE(FUNCNAME, RETURN_INTERFACE, MUST_CLASS)       \
//   ITPipeNode<RETURN_INTERFACE>* FUNCNAME(IPipeGraph* graph) {         \
//     VkPipeGraph* vkGraph = static_cast<VkPipeGraph*>(graph);          \
//     auto pipeNode = vkGraph->addNode<RETURN_INTERFACE, MUST_CLASS>(); \
//     return pipeNode.get();                                            \
//   }

IPipeGraph* createVkPipeGraph() {
  VkPipeGraph* graph = new VkPipeGraph();
  return graph;
}

// 初始化几何叠加管线，返回 IGeometryLayer（对象由 render 持有，调用方只借用）
IGeometryLayer* enableRenderGeometry(ISurfaceRender* render) {
  VkVideoRender* vkVideoRender = getVkVideoRender(render);
  if (!vkVideoRender) {
    return nullptr;
  }
  return vkVideoRender->enableRenderGeometry();
}
// 关闭叠加：VkGeometryLayer 移出 vulkan 执行链（对象不释放，随 render 回收）
void disableRenderGeometry(ISurfaceRender* render) {
  VkVideoRender* vkVideoRender = getVkVideoRender(render);
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->disableRenderGeometry();
}

// AVOX_GET_VK_NODE(getVkInputLayer, IVInputLayer, VkInputLayer)

// AVOX_GET_VK_NODE(getVkOutputLayer, IVOutputLayer, VkOutputLayer)

// AVOX_GET_VK_NODE(getVkYUV2RGBALayer, IYUVLayer, VkYUV2RGBALayer)

}
