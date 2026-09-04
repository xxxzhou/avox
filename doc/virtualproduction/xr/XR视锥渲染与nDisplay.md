> 整理自 aocec 仓库 `doc/virtualproduction/XR视锥渲染.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。合并自 2 篇。

## XR视锥渲染


## 外视锥

幕墙有多组LED屏幕合成,假定有4个,对应4个Mesh,每个mesh可以取周边四个点,根据这四个点与相机的位置生成透视投影矩阵,需要相机有多少Mesh,生成多少投影矩阵对应的viewport.

如果一台Mesh连接一台主机,则可以一台主机渲染一个viewport.如果需要保证渲染的图同步,需要使用如硬件genlock锁显卡,现在显卡支持这功能的一般不是家用的了,比较贵.

## 内视锥

首先幕墙的Mesh不影响内视锥的透视投影矩阵,内视锥的透视投影矩阵需要和真实环境下的相机匹配,FOV,姿态都是真实相机的,这时相机已经可以渲染成图.

如果确定每个Mesh上显示内视锥渲染图那一部分就是主要问题了.我想到的方案应该就是内视锥与每个Mesh组检测交互,每个Mesh可以得到碰撞点,根据碰撞点确定显示对应内视锥渲染图UV值.(曲线条是因为曲面显示吗?)

这里有个蛋疼的位置,明明每个Mesh只显示一部分,但是需要渲染整个内视锥,导致每台机器都需要渲染同一画面,如果只用一台电脑渲染一次,传输到别的电脑上,现阶段公开成熟的低延迟也只有NDI,但是至少1080P至少100MS的延迟也是不可接受的.现在都用Rivermax来解决这个问题,几个优点,直接GPU数据与光纤传输原始数据,节省内存交互时间和性能,也不需要编码与解码的时间及性能,相对应的,显卡,网卡,主板,交换机全有要求,一套并不便宜.

## 文档

[会魔法的XR演播室](https://zhuanlan.zhihu.com/p/501996958)


## nDisplay

## 文档

[nDisplay 技术](https://cdn2-unrealengine-1251447533.file.myqcloud.com/unrealengine-ndisplay-whitepaper-zhcn-041554733.pdf)

从本质上说，nDisplay 技术扩展了虚幻引擎，因为它能将摄像机画面的渲染任务分配给任意数量的计算机，并在任意数量的显示设备上显示图像。在全面考察后，我们认为在虚幻引擎中实现 nDisplay 的最佳方法就是让群集节点自动附加到虚幻引擎项目中当前活跃的摄像机位置上。虚幻引擎中的摄像机在拍摄画面后，该画面会基于 nDisplay 的设置进行扩展、渲染和分配。

## 代码逻辑

nDisplay实现在文档几乎没有的情况下,如何跟踪这个模块的实现,有个比较好的方法,你找到让你疑惑的一些点,并按照顺序写下来,跟着问题来找,并且你认为这个问题应该是如何解决的,然后去代码里找你猜的实现方式,如果不是,你也有种原来如此的印象,比如在这,nDisplay里,内视截是如何渲染LED上的?外视截应该是摄像机位置(nDisplay原点)与每块屏幕位置来确定对应LED块的渲染摄像机的角度,每块LED块对应摄像机使用不同角度渲染.那么内视截了,姿态是变化的,投射到LED幕墙范围是变化的,我的猜想是内摄像机以对应姿态渲染RTT,以对应姿态把RTT内容投射到LED上,如何把这RTT投射到变化的LED位置上?我暂时认为通过摄像机姿态计算LED范围,范围内显示点对应像素UV,然后找到RTT上的值.第二个问题是一台机器在渲染,还是多台渲染内视截.

IPDisplayClusterRenderManager: 

IPDisplayClusterClusterManager:

IPDisplayClusterConfigManager:

IPDisplayClusterGameManager: 

## 同步

NVApi,虚拟制片一般采用Genlock+NV菊花链同步方式,而NV菊花链基于Nvidia SwapLock API方式同步,[nDisplay中的同步机制](https://docs.unrealengine.com/4.26/zh-CN/WorkingWithMedia/nDisplay/Synchronization/)

``` C++
bool FDisplayClusterRenderSyncPolicyNvidia::InitializeNvidiaSwapLock(){
	using namespace DisplayClusterRenderSyncPolicyNvidia_Data_Windows;

	// Get D3D1XDevice
	D3DDevice = static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice());
	check(D3DDevice);
	
	// Get IDXGISwapChain
	DXGISwapChain = static_cast<IDXGISwapChain2*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference()->GetNativeSwapChain());
	check(DXGISwapChain);

    ...

	NvU32 MaxGroups = 0;
	NvU32 MaxBarriers = 0;

	// Get amount of available groups and barriers
	NvAPI_Status NvApiResult = NvAPI_D3D1x_QueryMaxSwapGroup(D3DDevice, &MaxGroups, &MaxBarriers);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't query group/barrier limits, error code 0x%x"), NvApiResult);
		return false;
	}

	// Make sure resources are available
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: max_groups=%d max_barriers=%d"), (int)MaxGroups, (int)MaxBarriers);
	if (!(MaxGroups > 0 && MaxBarriers > 0))
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: No available groups or barriers"));
		return false;
	}  

    ...

    	// Join swap group
	NvApiResult = NvAPI_D3D1x_JoinSwapGroup(D3DDevice, DXGISwapChain, RequestedGroup, true);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't join swap group %d, error code 0x%x"), RequestedGroup, NvApiResult);
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: Successfully joined the swap group %d"), RequestedGroup);
	}

	// Bind to sync barrier
	NvApiResult = NvAPI_D3D1x_BindSwapBarrier(D3DDevice, RequestedGroup, RequestedBarrier);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't bind group %d to swap barrier %d, error code 0x%x"), RequestedGroup, RequestedBarrier, NvApiResult);
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: Successfully bound group %d to the swap barrier %d"), RequestedGroup, RequestedBarrier);
	}

    ...  
        
}
```
