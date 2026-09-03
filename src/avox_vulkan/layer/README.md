# vulkan 的基本图元操作

## 功能介绍

(1) android vulkan 直接与 opengl 绑定.

(2) windows 如果 vulkan 与 dx11 交互完成,后续可以考虑放弃 dx11 相关模块开发

PS:

[dxgi_interop](https://github.com/krOoze/Hello_Triangle/blob/dxgi_interop/src/WSI/DxgiWsi.h)

[BindImageMemory](https://github.com/roman380/VulkanSdkDemos/blob/d3d11-image-interop/BindImageMemory2/BindImageMemory2.cpp#L154)

[dx11-vulkan-keymutex](https://github.com/KhronosGroup/VK-GL-CTS/blob/master/external/vulkancts/modules/vulkan/synchronization/vktSynchronizationWin32KeyedMutexTests.cpp)

[OpenCL Merging Roadmap into Vulkan](https://pcper.com/2017/05/follow-up-neil-trevett-and-tom-olson-from-khronos-group-discuss-opencl-and-vulkan-roadmap/)

## 注意点

每个 VkPipeGraph 肯定有个数据更新线程,每个层更新的参数可能不在这个数据更新线程上,更新了参数要么导致 vkPipeGraph 重置,要么置更新 UBO flag 为 true,然后更新 UBO 在数据更新线程上,这样可以避免很多可能的问题.

VkPipeGraph 提供延迟运行方式,由 delayGpu 控制,如果为 true,则输出结果是上一桢的运行数据生成的,否则当前桢的数据由当前桢数据运行生成.delayGpu 为 true,CPU 不需要在当前桢等待 GPU 运行结果,不过在 android 下图像短时间变化较大会有画面割裂的现象.

## VkLayer 几个重要时序点,大部分使用默认实现,有需要就 override

1 初始化.

一般指定使用的 shader 路径,UBO 大小,更新 UBO 内数据.

默认认为一个输入,一个输出,如果是多输入与多输出,可以在这指定,记着的输入/输出个数一定要在 onInitGraph 之前确定,相应数组会根据这二个值生成空间.

2 onInitGraph,当 vklayer 被添加到 VkPipeGraph 时上被调用.

一般用来加载 shader,根据输入与输出个数生成 pipelineLayout,如果有自己逻辑,请 override.

默认指定输入输出的的图像格式为 rgba8,如果不是,请在这指定对应图像格式.

如果层内包含别的处理层逻辑,请在这添上别的处理层.

参看[VkGuidedLayer](../aoce_vulkan_extra/layer/VkGuidedLayer.cpp).

3 onInitNode,当 onInitGraph 后被添加到 PipeGraph 后调用.

本身 layer 在 onInitGraph 后,onInitNode 前添加到 PipeGraph 了,当层内包含别的层时,用来指定层内之间的数据如何链接.

参看[VkGuidedLayer](../aoce_vulkan_extra/layer/VkGuidedLayer.cpp).

4 onInitLayer,当 PipeGraph 根据有序无环图中连线重新构建线性的执行顺序后.

根据各层是否启用等,PipeGraph 构建正确的各层执行顺序,在这里,每层都知道对应层数据的输入输出层,也知道输入输出层的大小.

当前层的输入大小默认等于第 0 个输入层的输出大小,并指定线程组的分配大小,如果逻辑需要变化,请在这里修改.

参看[VkReSizeLayer](layer/VkResizeLayer.cpp).

5 onInitBuffer,当所有有效层执行完后 onInitLayer 后,各层开始调用.

自动查找到输入层的输出 Texture,并生成本层的输出 Texture 给当前层的输出层使用.

如果自己有 Vulkan BUFFER 需要处理,请在 onInitVkBuffer 里处理.

参看[VkInputLayer](layer/VkInputLayer.cpp) [VkOutputLayer](layer/VkOutputLayer.cpp) [VkSeparableLinearLayer](../aoce_vulkan_extra/layer/VkSeparableLinearLayer.cpp).

6 onInitPipe,当本层执行完 onInitVkBuffer 后调用.

在这里,根据输入与输出的 Texture 自动更新 VkWriteDescriptorSet,并且生成 ComputePipeline.如果有自己的逻辑,请 override 实现.

参看[VkInputLayer](layer/VkInputLayer.cpp) [VkSeparableLinearLayer](../aoce_vulkan_extra/layer/VkSeparableLinearLayer.cpp) [VkSaveFrameLayer](../aoce_vulkan_extra/layer/VkLowPassLayer.hpp)

7 onCommand 当所有层执行完 onInitBuffer 后.

填充 vkCommandBuffer,vkCmdBindPipeline/vkCmdBindDescriptorSets/vkCmdDispatch 三件套.

参看[VkInputLayer](layer/VkInputLayer.cpp) [VkHistogramLayer](../aoce_vulkan_extra/layer/VkHistogramLayer.cpp)

8 onFrame 每桢处理时调用.

一般来说,只有输入层或输出层 override 处理,用于把 vulkan texture 交给 CPU/opengl es/dx11 等等.

参看[VkInputLayer](layer/VkInputLayer.cpp) [VkOutputLayer](layer/VkOutputLayer.cpp)

9 onUpdateParamet 独立上面的时间线,由用户更新层的参数时会调用.

但是实现并不会马上更新到 GPU 中,会改变状态,由 GPU 的运行 Command 线程发现状态变化后更新到 UBO BUFFER 中,避免一些 BUG.

一般来说,根据情况设置不同逻辑,请看下面章节 UBO.

## UBO 更新

UBO 一般来说,有四种.

1 CPU 参数与 GPU 参数对应,这种一般在初始化时调用 setUBOSize(int,true)/onUpdateParamet.可以使用宏 AOCE_VULKAN_PARAMETUPDATE 来实现 onUpdateParamet

2 CPU 参数与 GPU 参数不对应,但是不引起 PipeGraph 重置,在内部重新组织 GPU 对应数据结构 A,使用 setUBOSize(int)/updateUBO(&A).请在 onUpdateParamet 里实现 updateUBO(&A)/bParametChange=true.

3 参数引起 PipeGraph 重置,请在 onUpdateParamet 使用 resetGraph().

4 上面三种的组合,逻辑处理也是如上组合.

## 问题

1 VkPipeGraph 在运行时重启时,重启时会重置资源可能造成 vulkan device_lost.

(1) 假设因为在运行中,一个线程输出,一个线程运行,这二个线程访问的同一资源造成的？

尝试使用一个中间变量,输出的结果先输出到中间变量上,然后由中间变量输出到显示上,相应尝试结果把输出结果输出到中间变量是会导致上面的 device_lost 问题(非常奇怪,为什么,明明只是复制下数据),经测试,应该是 vkCmdBlitImage 这个 API 导致,换成 vkCmdCopyImage 可行,二者的区别难道不只是 vkCmdBlitImage 不需要二个图像一样大？但是还是不行,二个线程,必然要通过同一资源进行交换.

测试在二个线程 tick 中使用同步 mutex,暂时还没重现这个问题.优化这二者大范围同步,使用 VkEvent 来确定是否在重置资源中,线程输出检测在重置资源时,不作操作,这个操作后还没重现出这个问题.

2 DX11 与 Vulkan 交互蓝屏与无显示？

(1) VK submit 提交渲染管线时,有交互很容易导致 crash,机器蓝屏,猜想原因是 DX11 渲染与 VK 写入对接的 keymute 需要满足一要求一释放,而 cuda/dx11 可以自己控制.

先测试下,要保证一要求一释放,又要 dx11 与 vulkan 不在同一线程与同一频率,只能加入一个中间 dx11 纹理,测试看看效果.

我晕,导致 crash 主要是因为 vkCmdBlitImage 导致的,用 vkCmdCopyImage 无问题.

(2) 新问题,选择移动窗口会导致 vulkan timeout.

嗯,这个问题用上面的方法解决,加入中间层,原因应该就是如上需要满足计算与使用二线程无等待关系,那这样,直接用中间层,VK 运行线程直接写入结果,也不需要用 D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,改为 D3D11_RESOURCE_MISC_SHARED,用 vkFence 保证 GPU 里 VK CommandBuffer 执行顺序与 DX11 拷贝到临时变量的顺序.这样 VK 执行线程与 DX11 外部显示线程可以用不同频率跑.

3 vulkan_wrapper.h 在 android 可能会与别的库引起重命名函数问题,为避免这种情况,不要用头文件引用这个文件

4 只有I卡的集显,delayGpu为false时,每隔几帧,其executeOut里vkWaitForFences会导致等超10ms,而delayGpu为true,可以都在1ms内完成.

## Resize 重置大小

可以用完全的 imageLoad/imageStore 实现.

我尝试用 sampler 来实现,可以大大简化代码,性能提升并不确定,并且上面的方式有优化的空间,结果很奇怪,在运行时看不到结果全黑,但是用 RenderDoc 查看运行过程时,又能得到正确结果,这里先留个问号.

1 改 texture 为 texelFetch, 2 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 为 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE 有结果,但是不对,奇怪啊,这里应该为 sampler 才对,是什么导致这种现象,现测试 win/android 都可以得到结果.

上述问题解决: 创建的 texture 没有加上 VK_IMAGE_USAGE_SAMPLED_BIT 标记,导致使用使用 sampler 取不到数据.
