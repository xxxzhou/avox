> 整理自 aocec 仓库 `doc/virtualproduction/Rivermax开发.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。两篇合并(开发笔记 + 传输实现分析)。

## Rivermax开发

## 简介

通过网卡直接传输显卡原始数据,使渲染机之间延迟拉到最低.

[Rivermax模块代码](../../code/aoce_rivermax)

[Demo](../../samples/rivermaxtest)

## SDK解析

其设计以块Chunk为基础,块里包含适量的包Packet,包分Header信息与Payload信息.其Header只能在内存中,Payload部分可以使用GPU显存.其使用riverxmax在开始一个流时,声明存入块数据的MemBlock块,其头部和Payload可以使用同样MemBlock吗,也可以分开使用,如果Payload部分使用GPU显存,那么只能分开存放.MemBlock可以当做一个长宽块的BUFFER,其头部或是Payload包的Stride当做宽,而我们每次填充的数据/Stride做长度,假定MemBlock有A个Chunk,每个Chunk有B个Packet,那么MemBlock高度就是AxB的值,其字节大小AxBxStride,故第I个Chunk的地址为MemBlockPtr+IxBxStride,而Chunk里第Y个Packet地址为ChunkPtr+YxStride,对应Header/Payload填充每个Pactet里数据,然后让Rivermax以块Chunk提交.

Flow对应一个IP与端口,所有stream需要附加到Flow上,才能做到接受与输出,一个stream可能对应多个Flow发送/接收到不同网络位置.

TX流(out)表示输出流.输出流创建时根据相应MemBlock的属性以及其SDP信息(包含Flow信息),传输视频流时,得到当前视频流信息,把视频流的数据给到对应Chunk里的Packet,其RTP头部信息包含当前帧对应Packet里的如时间,包序列,帧尾等信息.

RX流(in)表示输入流,输入流创建根据Flow信息以及MemBlock的属性以及视频流信息,其接受视频流时,类似TX流,得到Chunk,Chunk里的包对应MemBlock的属性里地址,根据对应地址里的数据分析Header信息与Payload信息,根据Header信息得到是否当前帧最后的包,其发送时间与发送包的序列,得到这包在每帧数据中的位置,在相应位置把Payload信息复制到我们视频帧中.

## GPUDirect设计

我们假定传输渲染引擎中的纹理资源,如何把纹理资源直接使用Rivermax传输出去,如渲染引擎UE4,在其RHI线程中把相应纹理数据直接复制到Rivermax中的GPU显存中,然后在Rivermax发送数据线程发送出去,这样针对Rivermax使用的显存BUFFER,就有二个线程针对这显存BUFFER有读有写,如果全在RHI线程中,需要考虑下能否把Rivermax的发送放入UE4的RHI线程中,这样就有个问题,RHI线程费时需要加入Rivermax发送的时间,并且还要保证Rivermax GPUDirect可用,不然显存先download下内存中,然后rivermax发送,这个时间在RHI线程中是不可接受的,故最好使用二个线程,UE4只需要在RHI线程把纹理数据复制出来就行.这个思路的主要问题就是能否把RHI线程中的纹理得到Rivermax中的GPU显存中,以现在查得到资料来看是不可行,UE4在window下渲染使用Dx11/Dx12,其Dx11/Dx12纹理与Cuda显存资源交互是需要特定的Flag,而Rivermax的GPUDirect申请出来的显存资源也是有特定的Flag,故不可用.

其现在想法是加入一个中间显存资源,这个资源对应与dx11交互的特定显存资源,然后再把这个资源得到到Rivermax GPUDirect显存资源里,因为GPU显存之间超高的带宽,这个费时可以忽略不计,现主要问题就是交互纹理资源以使用长宽排列,而Rivermax的GPUDirect显存资源以Stride排列,此复制过程暂时也没查到CUDA直接可用的API,故自己用cuda写相关逻辑.

rivermax启用CUDA,要打开环境变量RIVERMAX_ENABLE_CUDA=1,整个开发大约有如下几点需要注意.

1. Rivermax需要开启VA(虚拟地址)以及gpuDirectRDMACapable功能,这个需要专业级显卡,一般主板GPU BAR1 memory默认只有256M,但是可供你申请RDMA显存只有26M左右,需要选择主板开启GPU BAR1 memory为4G,才能打开限制.

2. Rivermax支持多种SMPTE 2110协议,视频我们使用2110_20,里面使用SDP规范没说支持RGBA这种格式,但是我们使用UE4这种格式是最常见的,经我们测试,是可以使用RGBA这种格式的.

3. Rivermax发送BUFFER是需要根据你视频重组成包组,每个包对应一个Socket包,默认情况下其限制最大为1460,其包大小最好根据你每帧数据分解成整个包行,意思整包行*包大小=每帧数据,不是整行如果是用CPU传输也是可以的,但是GPURDMA接收会非常麻烦,其GPURDMA一般来说,是在申请BUFFER中的某块连续,如果是整行,你代码逻辑方面会节省不少功夫,需要主要在接受时注意,其BUFFER可能在你申请的最后几行有一部分,然后是最上面的一部分,这个地方需要单独注意,分二种情况收集GPU数据.

``` C++
if (dataBuffer->getBufferType() == BufferType::RDMA) {
    uint8_t* dataEndPtr =
        dataBuffer->getBufferPtr() + dataBuffer->getBufferSize();
    int32_t frameBufferSize = strideInFrame * dataStride;
    assert(videoStartPtr <= dataEndPtr);
    if (videoStartPtr == dataEndPtr) {
      videoStartPtr = dataBuffer->getBufferPtr();
    }
    // 查看当前帧是否连续的
    if (videoStartPtr + frameBufferSize <= dataEndPtr) {
      cuda::PtrStepSz<uchar> tempMat(strideInFrame, rawSize,
                                     (uchar*)videoStartPtr, dataStride);
      gpuCopyBuffer2Buffer(tempMat, outTexs[0]->getPtrStepSz(), stream);
    } else {
      // 帧在回环处,一部分在dataBuffer尾部,下一部分在开始处
      int32_t endSize = dataEndPtr - videoStartPtr;
      int32_t startSize = frameBufferSize - endSize;
      cuda::PtrStepSz<uchar> tempMat1(endSize / dataStride, rawSize,
                                      (uchar*)videoStartPtr, dataStride);
      cuda::PtrStepSz<uchar> tempMat2(startSize / dataStride, rawSize,
                                      (uchar*)dataBuffer->getBufferPtr(),
                                      dataStride);
      gpuCopyTwoBuffer2Buffer(tempMat1, tempMat2, outTexs[0]->getPtrStepSz(),
                              stream);
    }
    cudaDeviceSynchronize();
#if PERFORMANCE_MONITOR_RIVERMAX
    logPerformance("rivermax get gpu data time: ", highClock.recordClock());
#endif
  } 
```

## 使用协议说明

[简单介绍一下SDP规范](https://zhuanlan.zhihu.com/p/370460513)

[WebRTC之SDP篇](https://zhuanlan.zhihu.com/p/545040773)

[RTP有效负载](https://www.cnblogs.com/liushui-sky/p/13846456.html)

## 硬件信息查看

连接DUP: 使用PuTTY 登录对应COM口.

相看Windows上网口与硬件对应网口,在设备管理上右键属性里的Information里的Port Number.

查看网口: ethtool p0,查看丢包: ethtool -S p0 | grep drop

查看网卡状态: sudo mlxlink --show_fec -d mlx5_0,如果state显示disable,可能是光纤线没接好,现账号(ubuntu/nv4ieg-dpu).

windows下查看包情况: mlx5cmd -sniffer -name "以大网 8"

## Demo流程解析

线程分配: 线程数,发送流数量,工作流数量.每个线程分配发送流数量,每个线程分配工作流数量.每个发送流分配工作流数量.

其工作流(flow)分布在发送流(stream)之间,而发送流分布在线程之间,所以flows>=streams>=threads.

GenericSenderIONode: 对应一个线程,线程id,发送流数量,显存/内存分配器,发送/接收IP(FourTupleFlow),初始化所用的工作流.

注意点:

初始化工作流时,线程分配发送流,发送流分配工作流,工作流

申请显存: 使用aoce_cuda模块,申请的显存需要与网络硬件绑定,rmax_register_memory使用申请的显存地址与网络地址绑定,返回当前申请块的rmax_mkey_id.

创建流: rmax_out_create_gen_stream创建输出流,提供流参数,返回流id.

chunk长度: packet_num*iove_size*(app_header_size+pack_payload_size).

## 关联API

VirtualAlloc: 自身进程申请一块虚拟内存.MEM_RESERVE表示申请保留虚拟内存,MEM_COMMIT表示直接申请提交到物理内存的虚拟内存,

## 缩写

ST2110-20未压缩的视频流, 2110-22压缩流,2022-6,2022-8通过IP发送SDI流,其2022-8只支持DPU时间与PTP时钟,2022-7支持DUP冗余发送.

RGB8bit一个像素3字节,RGB10bit四个像素15字节(4 * 3 * 10=15 * 8),RGB12bit二个像素9个字节.

YUV422bit8二个像素4字节,YUV422bit10二像素5字节,二像素6字节.

[stop2110](https://stop2110.org/)

## 关键代码分析

RTPHeader需要12bytes,3个uint32.SRD header需要8-14 bytes.RTP_2110_20_STREAM_MIN_HEADER_SIZE = 20,下面描述每4byte对应的32bit描述信息.

``` c++
    // build RTP header - 12 bytes
    /*
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | V |P|X|  CC   |M|     PT      |            SEQ                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           timestamp                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           ssrc                                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
    buff[0] = 0x80;  // 10000000 - version2, no padding, no extension
    buff[1] = set.payload_type;
    buff[2] = (send_data.seq >> 8) & 0xff;  // sequence number
    buff[3] = (send_data.seq) & 0xff;  // sequence number
    *(uint32_t *)&buff[4] = htobe32((uint32_t)send_data.timestamp_tick);
    *(uint32_t *)&buff[8] = 0x0eb51dbd;  // simulated ssrc
    // build SRD header - 8-14 bytes
    /* 0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |    Extended Sequence Number   |           SRD Length          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |F|     SRD Row Number          |C|         SRD Offset          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
   buff[12] = (send_data.seq >> 24) & 0xff;  // high 16 bit of seq Extended Sequence Number
   buff[13] = (send_data.seq >> 16) & 0xff;  // high 16 bit of seq Extended Sequence Number
   *(uint16_t *)&buff[14] = htobe16(set.payload_size - RTP_2110_20_STREAM_MIN_HEADER_SIZE);  // SRD Length
   uint16_t number_of_rows = (set.video_type == VIDEO_TYPE::PROGRESSIVE ? set.height : set.height/2);
   uint16_t srd_row_number = (send_data.line % number_of_rows);
   *(uint16_t *)&buff[16] = htobe16(srd_row_number);
   buff[16] |= (send_data.m_second_field << 7);

   // we never have continuation
   *(uint16_t *)&buff[18] = htobe16(send_data.srd_offset);  // SRD Offset
```

其中0x0eb51dbd是RTP中SSRC,表示2110 specific header[RTP报文头中的SSRC和CSRC](https://blog.csdn.net/zhushentian/article/details/79804742).

为什么RGB对应的PaySize是RGB_DEFAULT_PAYLOAD_SIZE 1440,而YUV的PaySize是YUV_DEFAULT_PAYLOAD_SIZE 1200,根据什么划分的?

其1920*1080*n(RGB8-3,RGB10-15/4,RGB12-9/2,8YUV422-2,10YUV422-5/2,12YUV422-6/2),其1920/1440=4/3,1920/1200=8/5,所以1440/1200的选择是后续各格式可以除尽Height-1080最好的选择,一个Chunk可以表示整数行像素信息.PS(如上10YUV422中5/2表示2个像素占用5个Byte)

如何分配Chunk/Packet?先计算每帧有多少包,假定1080P下YUV422bit10则为1920 * 1080 * 5 / 2 / 1200 = 4320,每帧应该分成4320个包,这个每行4320/1080占用4个包,假定每个Chunk包含4行数据,这样一帧有1080/4=270个Chunk,这样一个Chunk应该包含16个包,每个包里1220(假定RTP头不分离,带20字节头信息)字节,但是包的长度需要与CPU的Cache line对齐,假定当前CPU的CacheLine为64字节,1200对齐后为1280.

继续如下分配,假定一个MemBlock存储块包含5帧数据,这样一个MemBlock就包含5 * 270 = 1350块Chunk,则chunks_num为1350,假定hdr/data合并,则data_size_arr应该是4320 * 5=21600个包,每个包有1220字节,data_size_arr=[1220,1220...]长度为21600的数组.

假定MaxBuffer包含50个MemBlock,如上chunk_size_in_strides表示每个Chunk几个包,如上是16,mem_block_array_len=50,data_stride_size为1280,app_hdr_stride_size=0.则allocate_and_register_memory分配的buffer总大小应该是 50*5*4320*1280=1,382,400,000.

``` C++
struct rmax_mem_block {
    void          *data_ptr;
    void          *app_hdr_ptr;
    uint16_t      *data_size_arr;
    uint16_t      *app_hdr_size_arr;
    size_t        chunks_num;
    rmax_mkey_id  data_mkey[RMAX_MAX_DUP_STREAMS];
    rmax_mkey_id  app_hdr_mkey[RMAX_MAX_DUP_STREAMS];
};
struct rmax_buffer_attr {
    size_t                chunk_size_in_strides;
    struct rmax_mem_block *mem_block_array;
    size_t                mem_block_array_len;
    uint16_t              data_stride_size;
    uint16_t              app_hdr_stride_size;
    rmax_out_buffer_attr_flags attr_flags;
};
typedef enum rmax_out_buffer_attr_flags_t {
    RMAX_OUT_BUFFER_ATTR_FLAG_NONE = 0x00,
    RMAX_OUT_BUFFER_ATTR_DATA_MKEY_IS_SET = 0x01,
    RMAX_OUT_BUFFER_ATTR_APP_HDR_MKEY_IS_SET = 0x02
} rmax_out_buffer_attr_flags;
```

RMAX_OUT_BUFFER_ATTR_DATA_MKEY_IS_SET/RMAX_OUT_BUFFER_ATTR_APP_HDR_MKEY_IS_SET在调用rmax_register_memory payload/header后使用.

## 启用GPU

media_receiver加入编译符ALLOW_OPENGL/CUDA_ENABLED,一个启用显示窗口,一个开启CUDA相关功能,其GPUDirect RDMA需要开启TCC模式.CMD里输入nvidia-smi可以查看显卡信息,NVIDIA Tesla/Quadro 系列高端 GPU 在 Windows 环境下可以配置为 Tesla 计算集群(Tesla Compute Cluster,简称 TCC)模式或 Windows 显示驱动模型（Windows Display Driver Model,简称 WDDM）模式两种模式有不同适用场景,在TCC该模式下,GPU 完全用于计算,不能作为本地显示输出,在WDDM该模式下,GPU 既用于计算又用于本地显示输出,切换至 WDDM 模式命令：nvidia-smi -dm 0,切换至 TCC 模式命令：nvidia-smi -dm 1.

## 编码优化

块(多个strides/packets)设计比较大,则rmax_out_get_next_chunk/rmax_out_commit/rmax_in_get_next_chunk 调用少,CPU占用底,但是会导致延迟更大.

## 开发BUG

注意,包大小的设置,对应输出流包data_size_arr里的大小为1920Byte,对面是收不到的,1440可以,所以这个大小要注意,找了二天.后面在文档[SMPTE STANDARD Professional Media Over Managed IP Networks: System Timing and Definitions](https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8165974)找到解释,其标准UDP包限制最大为1460.

接收端Buffer使用CPU分配,需要使用_aligned_malloc对齐CPU缓存大小(一般64Byte),否则创建接受流可能失败.


## Rivermax 图像传输

VP 拍摄里每个 Mesh 只显示一部分,但是需要渲染整个内视锥,导致每台机器都需要渲染一次内视锥,如果只用一台电脑渲染一次,传输到别的电脑上,现阶段公开的技术方案只有 NDI,但是 1080P 至少 100MS 的延迟也是不可接受的.业界现使用 Rivermax 来解决这个问题,相对 NDI 有几个优点,能直接显存对接网卡,把 GPU 数据通过光纤传输原始数据(无编码解码流程),节省内存交互时间和性能,也不需要编码与解码的时间及性能,相对应的,显卡,网卡,主板,交换机全有特殊要求,一套硬件并不便宜.

如下是开启 RDMA 后传输结果,延迟在一帧左右,这个 Demo 的实现在 UE5.1 之前,UE5.1 的 Rivermax 还不支持 RDMA.


## SDK 解析

其设计以块 Chunk 为基础,块里包含适量的包 Packet,包分 Header 信息与 Payload 信息.其 Header 只能在内存中,Payload 部分可以使用 GPU 显存.其使用 riverxmax 在开始一个流时,声明存入块数据的 MemBlock 块,其头部和 Payload 可以使用同样 MemBlock,也可以分开使用,如果 Payload 部分使用 GPU 显存,那么只能分开存放.

Flow 对应一个 IP 与端口,所有 stream 需要附加到 Flow 上,才能做到接受与输出,一个 stream 可能对应多个 Flow 发送/接收到不同网络位置.

TX 流(out)表示输出流.输出流创建时根据相应 MemBlock 的属性以及其 SDP 信息(包含 Flow 信息),传输视频流时,得到当前视频流信息,把视频流的数据给到对应 Chunk 里的 Packet,其 RTP 头部信息包含当前帧对应 Packet 里的如时间,包序列,帧尾等信息.

RX 流(in)表示输入流,输入流创建根据 Flow 信息以及 MemBlock 的属性以及视频流信息,其接受视频流时,类似 TX 流,得到 Chunk,Chunk 里的包对应 MemBlock 的属性里地址,根据对应地址里的数据分析 Header 信息与 Payload 信息,根据 Header 信息得到是否当前帧最后的包,其发送时间与发送包的序列,得到这包在每帧数据中的位置,在相应位置把 Payload 信息复制到我们视频帧中.

## RDMA 输入与接收

RDMA 里以 ST2110-20 协议传输原始图像数据,在 ST2110-20 标准中,RTP 被用来封装视频数据包,并通过 IP 网络进行传输,每个视频帧都被分割成一系列小的数据包,并使用 RTP 进行传输.因此主要 Rivermax 里针对 RTP 协议结构定义.

RTPHeader 需要 12bytes,3 个 uint32.SRD header 需要 8-14 bytes.RTP_2110_20_STREAM_MIN_HEADER_SIZE = 20,下面描述每 4byte 对应的 32bit 描述信息.

```c++
    // build RTP header - 12 bytes
    /*
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | V |P|X|  CC   |M|     PT      |            SEQ                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           timestamp                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           ssrc                                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
    buff[0] = 0x80;  // 10000000 - version2, no padding, no extension
    buff[1] = set.payload_type;
    buff[2] = (send_data.seq >> 8) & 0xff;  // sequence number
    buff[3] = (send_data.seq) & 0xff;  // sequence number
    *(uint32_t *)&buff[4] = htobe32((uint32_t)send_data.timestamp_tick);
    *(uint32_t *)&buff[8] = 0x0eb51dbd;  // simulated ssrc
    // build SRD header - 8-14 bytes
    /* 0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |    Extended Sequence Number   |           SRD Length          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |F|     SRD Row Number          |C|         SRD Offset          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
   buff[12] = (send_data.seq >> 24) & 0xff;  // high 16 bit of seq Extended Sequence Number
   buff[13] = (send_data.seq >> 16) & 0xff;  // high 16 bit of seq Extended Sequence Number
   *(uint16_t *)&buff[14] = htobe16(set.payload_size - RTP_2110_20_STREAM_MIN_HEADER_SIZE);  // SRD Length
   uint16_t number_of_rows = (set.video_type == VIDEO_TYPE::PROGRESSIVE ? set.height : set.height/2);
   uint16_t srd_row_number = (send_data.line % number_of_rows);
   *(uint16_t *)&buff[16] = htobe16(srd_row_number);
   buff[16] |= (send_data.m_second_field << 7);

   // we never have continuation
   *(uint16_t *)&buff[18] = htobe16(send_data.srd_offset);  // SRD Offset
```

其中 0x0eb51dbd 是 RTP 中 SSRC,表示 2110 specific header[RTP 报文头中的 SSRC 和 CSRC](https://blog.csdn.net/zhushentian/article/details/79804742).

最开始看他们例子里,其 RGB 对应的 PaySize 是 RGB_DEFAULT_PAYLOAD_SIZE 是 1440,而 YUV 的 PaySize 是 YUV_DEFAULT_PAYLOAD_SIZE 是 1200,没想明白,后面想到其 1920x1080xN(RGB8-3,RGB10-15/4,RGB12-9/2,8YUV422-2,10YUV422-5/2,12YUV422-6/2),其 1920/1440=4/3,1920/1200=8/5,所以 1440/1200 的选择是后续各格式可以除尽 Height-1080 最好的选择,一个 Chunk 可以表示整数行像素信息.PS(如上 10YUV422 中 5/2 表示 2 个像素占用 5 个 Byte).

RTP 选择 RTPHeader/Payload 分离,这样 CPU/RDMA 都支持,否则 RDMA 不支持.

如何分配 Chunk/Packet?先计算每帧有多少包,如上 data_size_arr 设定为 1200,那么 1080P 下 YUV422bit10 则为 1920x1080x5/2/1200 = 4320 包,每帧应该分成 4320 个包,这个每行 4320/1080 占用 4 个包,假定每个 Chunk 包含 4 行数据,这样一个 Chunk 应该包含 16 个包,这样一帧有 1080/4=270 个 Chunk,但是包的长度需要与 CPU 的 Cache line 对齐,假定当前 CPU 的 CacheLine 为 64 字节,1200 对齐后为 1280.

```C++
bool TxCuOutputLayer::initStream(const TxStreamSetting& vsetting) {
  frameRate.build(vsetting.rateMode);
  fpsInterval = frameRate.asInterval() * nanosecondsPerTick * ticksPerSecond;
  // flow保存
  flow = vsetting.flow;
  // 图像大小
  imageSize = getImageSize(inFormats[0]);
  // 选择的rawSize最好保持这个能整除宽
  // 所以rawSize大小最好选为width*每像素占用字节
#if FIXPAYLOAD
  int32_t rawSize = PAYLOAD_SIZE_RGBA;
  if (vsetting.pixelType != RMaxPixelType::rgba8) {
    rawSize = 1200;
  }
#else
  int32_t rawSize = getVideoRawSize(videoFormat);
  // rawSize这个值大于一定值后,对面接收不到
  while (rawSize > 1440) {
    rawSize = rawSize / 2;
  }
#endif
  // 包头信息大小
  setting.hdrSize = RTP_2110_20_STREAM_MIN_HEADER_SIZE;
  // 对齐CPU缓存,一般是64B
  setting.hdrStrideSize = rivermaxAlignPow2(setting.hdrSize);
  // 包头其有效数据
  setting.payloadSize = rawSize;
  // 对齐CPU缓存,一般是64B,(1200对齐后是1280)
  setting.strideSize = rivermaxAlignPow2(setting.payloadSize);
  // 每帧多少个包 4320
  setting.strideInFrame = divUp(imageSize, rawSize);
  // 每行多少包 4
  int32_t pactetsInLine = setting.strideInFrame / inFormats[0].height;
  // 计算每Chunk多少包=每chunk包含图像行x每行几个包(4x4= 16)
  setting.strideInChunk = vsetting.lineInChunk * pactetsInLine;
  // 每帧多少Chunk 270
  setting.chunkInFrame = inFormats[0].height / vsetting.lineInChunk;
  // 每个MemBlock包含多少帧
  setting.frameInMemBlock = vsetting.frameInMemBlock;
  // 每个MemBlock包含Chunk
  setting.chunkInMemBlock = setting.frameInMemBlock * setting.chunkInFrame;
  // 一个内存块就行
  setting.memBlockSize = vsetting.memBlockSize;
  return true;
}
```

继续如下分配,假定一个 MemBlock 存储块包含 5 帧数据,这样一个 MemBlock 就包含 5x270 = 1350 块 Chunk,则 chunks_num 为 1350,则 data_size_arr 应该是 4320x5=21600 个包,每个包有 1200 字节,data_size_arr=[1200,1200...]长度为 21600 的数组.

假定 MaxBuffer 包含 50 个 MemBlock,如上 chunk_size_in_strides 表示每个 Chunk 几个包,如上是 16,mem_block_array_len=50,data_stride_size 为 1280(相对 payloadSize 多了 80 的空 Byte),则 allocate_and_register_memory 分配的 buffer 总大小应该是 50x5x4320x1280=1,382,400,000.

```C++
bool TxCuOutputLayer::initBuffer() {
  // 一个memBlock多个包(4320x5)
  int32_t strideInMemBlock = setting.strideInFrame * setting.frameInMemBlock;
  // 每个memBlock需要申请的显存大小,1280x4320x5
  int32_t memDataSize = setting.strideSize * strideInMemBlock;
  // 所有memBlock需要的显存大小
  int32_t bufferSize = memDataSize * setting.memBlockSize;
  dataBuffer->create(bufferSize, paramet.bGpu);
  // 头部数据大小
  int32_t memHdrSize = setting.hdrStrideSize * strideInMemBlock;
  int32_t hdrSize = memHdrSize * setting.memBlockSize;
  // HDR头只能在内存上申请
  hdrBuffer->create(hdrSize, false);
  std::vector<uint16_t> payloadSizes(strideInMemBlock, setting.payloadSize);
  std::vector<uint16_t> hdrSizes(strideInMemBlock, setting.hdrSize);
  std::unique_ptr<rmax_mem_block[]> blocks(
      new rmax_mem_block[setting.memBlockSize]);
  // 空间设置
  uint8_t* dataPtr = dataBuffer->getBufferPtr();
  uint8_t* hdrPtr = hdrBuffer->getBufferPtr();
  for (int32_t i = 0; i < setting.memBlockSize; i++) {
    // 当前MemBlock里每个包的长度
    blocks[i].app_hdr_size_arr = hdrSizes.data();
    blocks[i].app_hdr_ptr = hdrPtr + i * memHdrSize;
    blocks[i].data_size_arr = payloadSizes.data();
    blocks[i].data_ptr = dataPtr + i * memDataSize;
    blocks[i].chunks_num = setting.chunkInMemBlock;
  }
  // MemBlock属性
  rmax_buffer_attr bufferAttr = {};
  bufferAttr.chunk_size_in_strides = setting.strideInChunk;
  bufferAttr.mem_block_array = blocks.get();
  bufferAttr.mem_block_array_len = setting.memBlockSize;
  bufferAttr.data_stride_size = setting.strideSize;
  bufferAttr.app_hdr_stride_size = setting.hdrStrideSize;
  bufferAttr.attr_flags = RMAX_OUT_BUFFER_ATTR_FLAG_NONE;
}
```

然后就是填充每帧的 RTP 头数据与显存数据,这里对照 Rivermax 官方例子就行,注意输入帧显存数据与填充到 Rivermax 里的 BUFFER 数据,都有可能其 Step 不等于 Width,如上面每行有 1280 字节,但是只有 1200 的数据是有效的,所以 CUDA 拷贝显示中做如下处理.

```C++
cuda::PtrStepSz<uchar> RivermaxBuffer::getPtrStepSz(int32_t payload,
                                                    int32_t stride) {
  int32_t height = divUp(bufferSize, stride);
  return cuda::PtrStepSz<uchar>(height, payload, (uchar*)buffer, stride);
}
 // 视频帧数据,GPU复制
    gpuCopyBuffer2Buffer(
        inTexs[0]->getPtrStepSz(),
        dataBuffer->getPtrStepSz(setting.payloadSize, setting.strideSize),
        stream);
    cudaStreamSynchronize(stream);
```

相应 CUDA 处理.

```cuda
const dim3 block = dim3(BLOCK_X, BLOCK_Y);

void gpuCopyBuffer2Buffer(PtrStepSz<uchar> source, PtrStepSz<uchar> dest,
                          cudaStream_t stream) {
  dim3 grid(divUp(source.width, block.x), divUp(source.height, block.y));
  copyBuffer2Buffer<<<grid, block, 0, stream>>>(source, dest);
}

void gpuCopyTwoBuffer2Buffer(PtrStepSz<uchar> source1, PtrStepSz<uchar> source2,
                             PtrStepSz<uchar> dest, cudaStream_t stream) {
  // 找个时间比较二种方式那个快
  // dim3 grid(divUp(source1.width, block.x),
  //          divUp(source1.height + source2.height, block.y));
  // copyTwoBuffer2Buffer<<<grid, block, 0, stream>>>(source1, source2, dest);
  dim3 grid(divUp(source1.width, block.x), divUp(source1.height, block.y));
  copyBuffer2Buffer<<<grid, block, 0, stream>>>(source1, dest);
  dim3 grid2(divUp(source2.width, block.x), divUp(source2.height, block.y));
  copyBuffer2Buffer<<<grid2, block, 0, stream>>>(source2, dest, source1.height);
}

inline __global__ void copyBuffer2Buffer(PtrStepSz<uchar> src,
                                         PtrStepSz<uchar> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  if (idx < src.width && idy < src.height) {
    int2 nuv = u12u2(u22u1(make_int2(idx, idy), src.width), dest.width);
    dest(nuv.y, nuv.x) = src(idy, idx);
  }
}

inline __global__ void copyBuffer2Buffer(PtrStepSz<uchar> src,
                                         PtrStepSz<uchar> dest,
                                         int srcStartHeight) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  if (idx < src.width && idy < src.height) {
    int2 nuv = u12u2(u22u1(make_int2(idx, idy + srcStartHeight), src.width),
                     dest.width);
    dest(nuv.y, nuv.x) = src(idy, idx);
  }
}

inline __global__ void copyTwoBuffer2Buffer(PtrStepSz<uchar> src1,
                                            PtrStepSz<uchar> src2,
                                            PtrStepSz<uchar> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  if (idx < src1.width && idy < src1.height) {
    int2 nuv = u12u2(u22u1(make_int2(idx, idy), src1.width), dest.width);
    dest(nuv.y, nuv.x) = src1(idy, idx);
  }
  if (idx < src1.width && idy >= src1.height) {
    int2 nuv =
        u12u2(u22u1(make_int2(idx, idy + src1.height), src1.width), dest.width);
    dest(nuv.y, nuv.x) = src2(idy, idx);
  }
}
```

接收的数据主要考虑回环的问题,比如接收的数据有一部分在当前块的最后位置,而下一部分在块的最上面,别的参照官方例子就行.

```C++
bool RxCuInputLayer::onFrame() {
#if PERFORMANCE_MONITOR_RIVERMAX
  HighClock highClock = {};
#endif
  if (dataBuffer->getBufferType() == BufferType::RDMA) {
    uint8_t* dataEndPtr =
        dataBuffer->getBufferPtr() + dataBuffer->getBufferSize();
    int32_t frameBufferSize = strideInFrame * dataStride;
    assert(videoStartPtr <= dataEndPtr);
    if (videoStartPtr == dataEndPtr) {
      videoStartPtr = dataBuffer->getBufferPtr();
    }
    // 查看当前帧是否连续的
    if (videoStartPtr + frameBufferSize <= dataEndPtr) {
      cuda::PtrStepSz<uchar> tempMat(strideInFrame, rawSize,
                                     (uchar*)videoStartPtr, dataStride);
      gpuCopyBuffer2Buffer(tempMat, outTexs[0]->getPtrStepSz(), stream);
    } else {
      // 帧在回环处,一部分在dataBuffer尾部,下一部分在开始处
      int32_t endSize = dataEndPtr - videoStartPtr;
      int32_t startSize = frameBufferSize - endSize;
      cuda::PtrStepSz<uchar> tempMat1(endSize / dataStride, rawSize,
                                      (uchar*)videoStartPtr, dataStride);
      cuda::PtrStepSz<uchar> tempMat2(startSize / dataStride, rawSize,
                                      (uchar*)dataBuffer->getBufferPtr(),
                                      dataStride);
      gpuCopyTwoBuffer2Buffer(tempMat1, tempMat2, outTexs[0]->getPtrStepSz(),
                              stream);
    }
    cudaDeviceSynchronize();
#if PERFORMANCE_MONITOR_RIVERMAX
    logPerformance("rivermax get gpu data time: ", highClock.recordClock());
#endif
  } else {
    outTexs[0]->upload(cpuFrameBuffer->getBufferPtr(), 0, stream);
#if PERFORMANCE_MONITOR_RIVERMAX
    logPerformance("rivermax get cpu data time: ", highClock.recordClock());
#endif
  }
  return true;
}
```

注意,包大小的设置,对应输出流包 data_size_arr 大小为 1920Byte,对面是收不到的,而 1440 可以,所以这个大小要注意,查了一天.后面在文档[SMPTE STANDARD Professional Media Over Managed IP Networks: System Timing and Definitions](https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8165974)找到解释,其标准 UDP 包限制最大为 1460.

接收端如果 Buffer 使用 CPU 分配,需要使用\_aligned_malloc 对齐 CPU 缓存大小(一般 64Byte),否则创建接受流可能失败.

## DX12/CUDA

如何直接使用 UE 的纹理输入,或者是把接收的 RDMA 数据输出到 UE 的纹理上,毕竟上面针对 RDMA 里的处理都是 CUDA 的显存数据,但是 UE 里底层使用 DX11/DX12,所以需要考虑高效稳定直接传输,不然把 DX11/DX12-内存-CUDA 的话,就完全没必要使用 RDMA 了.

DX11 与 CUDA 交互相对简单,但是 UE5 之后,默认改为 DX12 渲染,DX12 的显存 CopyBuffer 也需要放在 CommandList 中,这样同步就相对麻烦些.

查看官方 CUDA 的例子,DX12 的 NT 共享句柄与 CUDA 使用 cudaExternalMemory 只看到 Buffer 资源可以,纹理没测试成功,按理说应该是可行的,暂时先通过 DX12 与 DX11 使用 NT 句柄交互,使用 ID3D11Fence 同步.

UE 发送 DX12 纹理给 Rivermax 的 CUDA 使用.

1. Rivermax 处理模块,申请一个 DX11 上下文,包含一个 DX11 共享纹理使用 NT(与 DX12 交互,带共享 NT 句柄 ID3D11Fence),一个 DX11 共享纹理不使用 NT(与 CUDA 交互映射).
2. 在 UE 的 RHI 线程上,得到 DX12 相关 Device/CommandQueue 等信息,根据上面纹理与 ID3D11Fence 的共享 NT 句柄,得到对应的 ID3D12Resource/ID3D12Fence,使用 ID3D12Fence 同步资源.
3. 在 Rivermax 处理线程上,根据纹理与 ID3D11Fence 的共享 NT 句柄,得到对应的 ID3D11Texture2D/ID3D11Fence,进行同步复制到非 NT 句柄的 DX11 上的 ID3D11Texture2D,并把此 ID3D11Texture2D 与 CUDA 映射与处理.

Rivermax 发送 CUDA 显存给 UE 的 DX12 纹理.

1. Rivermax 处理模块,申请一个 DX11 上下文,包含一个 DX11 共享纹理使用 NT(与 DX12 交互,带共享 NT 句柄 ID3D11Fence),一个 DX11 共享纹理不使用 NT(与 CUDA 交互).
2. 在 Rivermax 处理线程上,调用 NT 的 ID3D11Fence 同步,把与 CUDA 映射的非 NT 的 DX11 资源复制到 NT 的 DX11 的 ID3D11Texture2D 上.
3. 在 UE4 的 RHI 线程上,得到 DX12 相关 Device/CommandQueue 等信息,并根据 Rivermax 处理模块的 DX11 纹理与 ID3D11Fence 的共享 NT 句柄,得到对应 ID3D12Resource/ID3D12Fence,根据 ID3D12Fence 同步资源,并把 ID3D12Resource 复制给 UE 纹理.

贴一部分代码,后续代码整理后放到 github 上.

首先如何拿到 UE 的 Native 资源.

```C++
void UAoceFunctionLibrary::GetTextureRHIRef(FTexture2DRHIRef& TextureRef, UTextureRenderTarget2D* RenderTarget2D)
{
	ENQUEUE_RENDER_COMMAND(GetRTTextureRHICommand)
	([&TextureRef,RenderTarget2D](FRHICommandListImmediate& RHICmdList)
	{
		if (!RenderTarget2D) { return; }
		FTextureRenderTargetResource* RenderResource = RenderTarget2D->GetRenderTargetResource();
		if (!RenderResource) { return; }
		TextureRef = RenderResource->GetTextureRenderTarget2DResource()->GetTextureRHI();
	});
	FlushRenderingCommands();
}

void UAoceFunctionLibrary::GetTextureRHIRef(FTexture2DRHIRef& TextureRef, UTexture* Texture2D)
{
	ENQUEUE_RENDER_COMMAND(GetTextureRHICommand)
	([&TextureRef,Texture2D](FRHICommandListImmediate& RHICmdList)
	{
		if (!Texture2D) { return; }
		TextureRef = Texture2D->GetResource()->GetTexture2DRHI();
	});
	FlushRenderingCommands();
}

void UAoceFunctionLibrary::InputGPUData(aoce::IInputLayer* InputLayer, FTexture2DRHIRef& TextureRef)
{
	ENQUEUE_RENDER_COMMAND(InputGPUDataCommand)
	([InputLayer,TextureRef](FRHICommandListImmediate& RHICmdList)
	{
		if (!TextureRef) { return; }
		aoce::RenderTexture renderTexure = {};
		if (IsDX12)
		{
			aoce::DX12Context context = {};
			context.device = RHICmdList.GetNativeDevice();
			context.queue = RHICmdList.GetNativeGraphicsQueue();
			renderTexure.context = &context;
			renderTexure.texture = TextureRef->GetNativeResource();
			InputLayer->inputGpuData(renderTexure);
		}
		else
		{
			renderTexure.context = RHICmdList.GetNativeDevice();
			renderTexure.texture = TextureRef->GetNativeResource();
			InputLayer->inputGpuData(renderTexure);
		}
	});
}

void UAoceFunctionLibrary::OutputGPUData(aoce::IOutputLayer* OutputLayer, FTexture2DRHIRef& TextureRef)
{
	ENQUEUE_RENDER_COMMAND(OutputGPUDataCommand)
	([OutputLayer,TextureRef](FRHICommandListImmediate& RHICmdList)
	{
		if (!TextureRef) { return; }
		aoce::RenderTexture renderTexure = {};
		if (IsDX12)
		{
			aoce::DX12Context context = {};
			context.device = RHICmdList.GetNativeDevice();
			context.queue = RHICmdList.GetNativeGraphicsQueue();
			renderTexure.context = &context;
			renderTexure.texture = TextureRef->GetNativeResource();
			OutputLayer->outGpuData(renderTexure);
		}
		else
		{
			renderTexure.context = RHICmdList.GetNativeDevice();
			renderTexure.texture = TextureRef->GetNativeResource();
			OutputLayer->outGpuData(renderTexure);
		}
	});
}
```

底层管线初始化.

```C++
    // DX12使用一个NT共享句柄中转
    if (cuPipeGraph->getRenderType() == RenderType::dx12) {
      tempNTTex->restart(cuPipeGraph->getDX11Device(), inFormats[0].width,
                         inFormats[0].height, dxFormat,
                         DX11SharedType::sharedNT);
      shardTex->restart(cuPipeGraph->getDX11Device(), inFormats[0].width,
                        inFormats[0].height, dxFormat, DX11SharedType::shared);
      // 绑定一个DX11共享资源与CUDA资源
      // registerCudaResource(cudaResoure, shardTex.get());
    } else if (cuPipeGraph->getRenderType() == RenderType::dx11) {
      // 非NT资源与CUDA资源绑定
      shardTex->restart(cuPipeGraph->getDX11Device(), inFormats[0].width,
                        inFormats[0].height, dxFormat,
                        DX11SharedType::sharedmutex);
      // 绑定一个DX11共享资源与CUDA资源
      // registerCudaResource(cudaResoure, shardTex.get());
    }
    // 绑定一个DX11共享资源与CUDA资源
    cudaResoure->registerDxResource(shardTex.get());
```

每帧交互切换.

```C++
 // DX12交互要求使用NT句柄共享Buffer/Fence
    if (cuPipeGraph->getRenderType() == RenderType::dx12) {
      ID3D11Device* d3ddevice = cuPipeGraph->getDX11Device();
      tempNTTex->updateContext(d3ddevice);
      CComPtr<ID3D11DeviceContext> d3dcontext = nullptr;
      d3ddevice->GetImmediateContext(&d3dcontext);
      CComPtr<ID3D11DeviceContext4> d3dcontext4 = nullptr;
      d3dcontext->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                 (void**)&d3dcontext4);
      ID3D11Fence* fence = tempNTTex->sharedFence;
      // 设计成不等待,二种状态
      uint64_t currentFence = fence->GetCompletedValue();
      if (currentFence % 2 == AOCE_DX11_MUTEX_WRITE) {
        // 把CUDA计算结果放入非NT DX11 Source
        // gpuMat2D3dTexture(inTexs[0], cudaResoure, stream);
        cudaResoure->copyFormMat(inTexs[0], stream);
        // 从非NT DX11 Source复制数据到NT DX11 Source
        d3dcontext->CopyResource(tempNTTex->texture->texture,
                                 shardTex->texture->texture);
        // 从结果上来看,设定的fence value不能少于自身
        d3dcontext4->Signal(fence, currentFence + 1);
      }
      tempNTTex->bGpuUpdate = true;
#if PERFORMANCE_MONITOR_CUDA
      logPerformance("cuda to dx12 time: ", highClock.recordClock());
#endif
    } else if (cuPipeGraph->getRenderType() == RenderType::dx11) {
      CComPtr<IDXGIKeyedMutex> pDX11Mutex = nullptr;
      HRESULT hResult = shardTex->texture->texture->QueryInterface(
          __uuidof(IDXGIKeyedMutex), (LPVOID*)&pDX11Mutex);
      if (FAILED(hResult)) {
        logHResult(hResult, "cuda outputlayer get keyed mutex failed");
        return true;
      }
      DWORD result = pDX11Mutex->AcquireSync(AOCE_DX11_MUTEX_WRITE, 0);
      // shardTex->texture映射cudaResoure
      if (result == WAIT_OBJECT_0) {
        // gpuMat2D3dTexture(inTexs[0], cudaResoure, stream);
        cudaResoure->copyFormMat(inTexs[0], stream);
      }
      result = pDX11Mutex->ReleaseSync(AOCE_DX11_MUTEX_READ);
      shardTex->bGpuUpdate = true;
#if PERFORMANCE_MONITOR_CUDA
      logPerformance("cuda to dx11 time: ", highClock.recordClock());
#endif
    }
```

如上过程完成了,但是有新问题,在共享纹理与 DX12 交互的那一步中,有个问题,当 DX12 从纹理共享 NT 句柄得到资源并放入 CommandList 后,资源就销毁了,而后在 UE 的渲染线程执行 CommandList 时就会出现问题,而从共享 NT 句柄得到的 ID3D12Fence 是能销毁的(可能是放入的 Queue,里面复制了数据),再次改进,不拿到 UE 的 CommandList,而是用 UE4 的 Queue 创建一个 CommandList,可以设置纹理 barrier 以及同步完,这样就能保证在复制时执行完成,和 DX11 实现类似.

```C++
bool Dx12Command::initCommand() {
  if (!device) {
    return true;
  }
  // D3D12_COMMAND_LIST_TYPE_COPY D3D12_COMMAND_LIST_TYPE_DIRECT
  D3D12_COMMAND_LIST_TYPE commandType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  HRESULT hr = device->CreateCommandAllocator(commandType,
                                              IID_PPV_ARGS(&commandAllocator));
  if (FAILED(hr)) {
    logHResult(hr, "dx12 create command allocator failed");
    return false;
  }
  // 创建CommandList
  hr = device->CreateCommandList(0, commandType, commandAllocator, nullptr,
                                 IID_PPV_ARGS(&commandList));
  if (FAILED(hr)) {
    logHResult(hr, "dx12 create command list failed");
    return false;
  }
  commandList->SetName(L"Dx12Command commandList");
  commandList->Close();
  // 创建同步
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    logHResult(hr, "dx12 create fence failed");
    return false;
  }
  fenceValue = 1;
  fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  return true;
}
void Dx12Command::closeCommand() {
  if (!queue || !commandList) {
    return;
  }
  // 等待完成并关闭ID3D12CommandAllocator
  // 当队列所有命令完成时,fence为fenceValue
  queue->Signal(fence, fenceValue);
  // fence信号为fenceValue时触发
  fence->SetEventOnCompletion(fenceValue, fenceEvent);
  // 等待fenceEvent触发
  WaitForSingleObject(fenceEvent, INFINITE);
  // 重置命令列表
  commandAllocator->Reset();
  if (fenceEvent) {
    CloseHandle(fenceEvent);
    fenceEvent = nullptr;
  }
  // 关闭命令列表
  commandAllocator.Release();
  commandList.Release();
}
```

## YVU10/RGBA 的 GPGPU 处理

在现代硬件能力的加强下,出现视频的分辨率越来越大,颜色细节也更丰富,比如有屏幕宣传他家屏幕超 10 亿种颜色,我们知道 RGB8 下,一共 256^3 种组合只有 16KW 种颜色,如果要超 10 亿,需要提高每个通道更高 BIT,如 RGB10B 下,就有超 10 亿种颜色.

如今也越来越多 10bit 视频,在编写相关 Rivermax 程序中,需要添加其 CUDA 对应的 YUV10B 与 RGBA 互相转化处理,记录下.

在这我们以 YUV422 交叉格式的 10BIT 转 RGBA8 做为例子,说明下如何使用 CUDA 来完成,以 1080P 为例,其 YUV10BIT 下,每二个像素二个 Y,共用一个 UV,一共四个元素,也就是 4x10BIT,一共 40BIT,对应五个 8BIT,也就是说,如果把 YUV10BIT 的 422 交叉格式图像当成 R(8BIT)单通道来看的话,其宽度应该为 1920\*5/2Byte,高度不变,对应一张 4800x1080 的 R 单通道 8BIT 图.

在处理这种所有数据相互间无关系,GPU 最容易处理了,首先划分组来处理,分组在这种数据间无关系情况下,只需要注意多个核心不写同样显存位置就好了.

```c++
void yuv2rgb_gpu(PtrStepSz<uchar> source, PtrStepSz<uchar4> dest,
                 int32_t yuvtype, cudaStream_t stream) {
    dim3 grid(divUp(dest.width / 2, block.x), divUp(dest.height, block.y));
    yuv10b2rgb<<<grid, block, 0, stream>>>(source, dest);
}
```

说明下代码,以 1080P 为例,输入的 source 是 4800x1080 的 R8 图像,结果是 1920x1080 的 RGBA8 图像,其分成(1920/2/16=60,1080/16=68)共 4080 个块进行处理.

```c++
// 10bitYUV422
inline __global__ void yuv10b2rgb(PtrStepSz<uchar> source,
                                  PtrStepSz<uchar4> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  const uint16_t mask10bit = 0x03FF;
  // 一次处理二个像素,对应source中的5个byte
  if (idx < dest.width / 2 && idy < source.height) {
    // 对于小于uint32类型,左移都会提升到uint32
    // https://www.coder.work/article/7301177
    uint16_t u =
        ((source(idy, idx * 5) << 2) | (source(idy, idx * 5 + 1) >> 6)) &
        mask10bit;
    uint16_t y1 =
        ((source(idy, idx * 5 + 1) << 4) | (source(idy, idx * 5 + 2) >> 4)) &
        mask10bit;
    uint16_t v =
        ((source(idy, idx * 5 + 2) << 6) | (source(idy, idx * 5 + 3) >> 2)) &
        mask10bit;
    uint16_t y2 =
        ((source(idy, idx * 5 + 3) << 8) | (source(idy, idx * 5 + 4))) &
        mask10bit;
    ushort4 suyuv = make_ushort4(u, y1, v, y2);
    float4 yuyv = uint102float4(suyuv);
    float3 yuv = make_float3(yuyv.y, yuyv.x, yuyv.z);
    dest(idy, idx * 2) = rgbafloat42uchar4(make_float4(yuv2Rgb(yuv), 1.f));
    float3 yuv1 = make_float3(yuyv.w, yuyv.x, yuyv.z);
    dest(idy, idx * 2 + 1) = rgbafloat42uchar4(make_float4(yuv2Rgb(yuv1), 1.f));
  }
}
```

顺便解释下,前面 1920 为什么要除 2,因为 YUV 是二个像素共用 UV,如果不除,每个像素多算了一次 UV.至于里面进位关系,参考下面手绘图.


同样,RGBA8BIT 转 YUV10BIT 如下.

```C++
void rgb2yuv_gpu(PtrStepSz<uchar4> source, PtrStepSz<uchar> dest,
                 int32_t yuvtype, cudaStream_t stream) {
    dim3 grid(divUp(source.width / 2, block.x), divUp(source.height, block.y));
    rgb2yuv10B<<<grid, block, 0, stream>>>(source, dest);
}
inline __global__ void rgb2yuv10B(PtrStepSz<uchar4> source,
                                  PtrStepSz<uchar> dest) {
  const int idx = blockDim.x * blockIdx.x + threadIdx.x;
  const int idy = blockDim.y * blockIdx.y + threadIdx.y;
  if (idx < source.width / 2 && idy < source.height) {
    float4 rgba1 = rgbauchar42float4(source(idy, idx * 2));
    float4 rgba2 = rgbauchar42float4(source(idy, idx * 2 + 1));
    float3 yuv1 = rgb2Yuv(make_float3(rgba1));
    float3 yuv2 = rgb2Yuv(make_float3(rgba2));
    // UYVY
    float4 ryuyv = make_float4((yuv1.y + yuv2.y) / 2.f, yuv1.x,
                               (yuv1.z + yuv2.z) / 2.f, yuv2.x);
    ushort4 syuyv = float42uint10(ryuyv);
    // 4个10有效位的数据分成5个uint8
    uint8_t b1 = (syuyv.x >> 2) & 0xFF;
    uint8_t b2 = ((syuyv.x << 6) | (syuyv.y >> 4)) & 0xFF;
    uint8_t b3 = ((syuyv.y << 4) | (syuyv.z >> 6)) & 0xFF;
    uint8_t b4 = ((syuyv.z << 2) | (syuyv.w >> 8)) & 0xFF;
    uint8_t b5 = syuyv.w & 0xFF;
    dest(idy, idx * 5) = b1;
    dest(idy, idx * 5 + 1) = b2;
    dest(idy, idx * 5 + 2) = b3;
    dest(idy, idx * 5 + 3) = b4;
    dest(idy, idx * 5 + 4) = b5;
  }
}
```
