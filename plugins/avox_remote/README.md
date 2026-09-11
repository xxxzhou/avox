# avox_remote

远程内容源插件: 实现 `AvoxBase.h` 的统一接口 `IRemoteSource`(磁力/DAV/SMB/网盘/
媒体服务器同一套 open→list→resolve 模型), 由 `createRemoteSource(type)` 创建。

## 当前实现

| type | 协议 | 说明 |
|------|------|------|
| `"dav"` | WebDAV | PROPFIND 目录树 + Basic 认证; 兼容 Alist/OpenList 的 `/dav` 端点(坚果云/极空间/群晖等标准 WebDAV 同理) |

用法(以 dav 为例):

```cpp
avox::IRemoteSource* src = avox::createRemoteSource("dav");  // 插件未装返回 nullptr
avox::IRemoteSourceOb* ob = ...;   // onOpenResult/onListResult 回调
src->setOb(ob);
src->setParam("verifyTls", "false");   // 自签名证书的内网 NAS 场景(可选)
src->open("https://dav.example.com/dav/", "user", "pass", nullptr, 10000);
// onOpenResult(0) 后:
src->list("");                        // 列根; 目录 token 带尾'/', 下钻即 list(token)
int n = src->getEntryCount();         // type: dir/media/file, 目录排前名称升序
src->getEntryName(i); src->getEntryToken(i); src->getEntrySize(i); src->getEntryType(i);
src->getSessionField("name");         // 根目录显示名(另有 "host")
const char* url = src->resolve(i, nullptr);  // http(s) 直链, 现有 http IO 直接播
src->close();                          // 会话结束(create* 产物记得释放)
```

要点:
- **播放通路**: resolve 产出 `http(s)://user:pass@host/path` 直链(鉴权以 userinfo 内嵌,
  FFmpeg http 协议原生支持), 交 `IMediaPlayer::open` 走现有 http IO, 无需额外选项键;
- **token 约定**: 解码后的服务器绝对路径(目录带尾 `/`), 上层当不透明串传回即可;
- **百分比编码**: 请求路径逐段重编码, href 返回的编码串自动解码, 空格/中文/特殊字符均覆盖;
- **错误码映射**: 401/403→`authFailed(-3)`(账密错), 404→`notFound(-6)`, 网络错→`net(-5)`,
  其余 HTTP 错→`other(-8)`; 超时经 `onOpenResult/onListResult` 的 `timeout(-2)` 上报;
- **中止语义**: httplib 请求本身不可中断, `stopList/close` 在当前请求返回后生效(结果丢弃不回调);
- **会话参数**: `setParam("verifyTls", "false")` 关闭 TLS 证书校验(自签名场景);
- godot 封装: `RemoteSource` 类(`platform/godot/plugin/src/remote_source.h`)已接本插件,
  tools 播放器 UI 的磁力流程同一套接口可平移到 WebDAV。

## 规划: "smb" (libsmb2)

SMB 走 libsmb2(LGPL, 纯 C 异步 API), 依赖预编译入库方式与 libtorrent 一致
(源项目独立 `python build_windows.py` → 产物进 `avc_library/3rdparty/library/windows/libsmb2/`,
本仓库 `cmake/FindLibsmb2.cmake` 查找)。库就位后在 `RemoteModule::loadModule` 注册 `"smb"`,
实现 `SmbSource`(libsmb2 读字节 → 自定义 avio → 现有解封装管线), 接口零改动。

## 构建

依赖: cpp-httplib(仓库内 header-only) + OpenSSL 3.0+(全局 find_package, 未找到自动跳过本插件)。
Windows 下附加 ws2_32/crypt32。产物: `<输出>/plugins/avox_remote.dll`, ModuleMgr 懒加载
(`createRemoteSource("dav")` 首调触发)。

## 测试

`script/` 外的本地冒烟: `D:\tmp\davtest\`(mock_dav.py 最小 WebDAV 服务器 + davtest.exe),
覆盖 错密码/列根/下钻/resolve 直链/目录拒解析/404 六条路径, 16 项断言全过。
