> 整理自 aocec 仓库 `doc/math/g2o.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# g2o

## 方法

SE3Quat.log() 欧氏变换使用对数映射得到它的李代数.

SE3Quat.exp() 由李代数使用指数映射得到它的欧氏变换.

## 概念

雅克比矩阵存储了误差项的每一维相对于顶点各优化成员的偏导数.

``` c++
// D对应观测值维度(_error的个数),E对应观测值类型,VertexXi对应第一个点的类型,VertexXj第二个点的类型.
template <int D, typename E, typename VertexXi>
class BaseUnaryEdge : public BaseFixedSizedEdge<D, E, VertexXi> ;

template <int D, typename E, typename VertexXi, typename VertexXj>
class BaseBinaryEdge : public BaseFixedSizedEdge<D, E, VertexXi, VertexXj> ;
```

其中一元边BaseUnaryEdge只有_jacobianOplusXi,对应_jacobianOplus[0],

二元边BaseBinaryEdge有_jacobianOplusXi/_jacobianOplusXj,对应_jacobianOplus[0]/_jacobianOplus[1].

多元边BaseTebMultiEdge直接配置对应_jacobianOplus,resize设置顶点个数.

## 特殊的类

1. 动态变化顶点的边.

``` C++
class G2O_TYPES_SLAM3D_API EdgeSE3LotsOfXYZ
    : public BaseVariableSizedEdge<-1, VectorX> {
 protected:
  unsigned int _observedPoints;

 public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
  EdgeSE3LotsOfXYZ();

  void setSize(int vertices) {
    resize(vertices);
    _observedPoints = vertices - 1;
    _measurement.resize(_observedPoints * 3, 1);
    setDimension(_observedPoints * 3);
  }

  ....
}
```
