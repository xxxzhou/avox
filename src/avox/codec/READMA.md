# 文档

## H264

主要参考[H26X](https://github.com/HR1025/MMP-H26X) 做H264/H265的解析

[一篇文让你看懂H264 编解码协议](https://zhuanlan.zhihu.com/p/478741699)

[h.264参考图像列表、解码图像缓存](https://www.cnblogs.com/TaigaCon/p/3715276.html)

[H.264 入门篇 - 09 (帧间预测 - 参考帧列表)](https://blog.csdn.net/zhoutaopower/article/details/127342301)

每个可解码的图片过程。

1. 根据pic_order_cnt_type确定计算POC的方式。
2. 如果是P/B帧,构建参考帧列表，短期参考帧在一周其内，其周期一般在16帧内(sps->log2_max_frame_num_minus4)。
3. 根据当前帧更新参考帧列表。