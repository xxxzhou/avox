# avox_remote

远程内容源插件: 实现 `AvoxBase.h` 的统一接口 `IRemoteSource`(磁力/DAV/SMB/网盘/
媒体服务器同一套 open→list→resolve 模型), 由 `createRemoteSource(type)` 创建。

## 当前实现

| type | 协议 | 说明 |
|------|------|------|
| `"dav"` | WebDAV | PROPFIND 目录树 + Basic 认证; 兼容 Alist/OpenList 的 `/dav` 端点(坚果云/极空间/群晖等标准 WebDAV 同理) |
| `"smb"` | SMB2/3 | libsmb2(LGPL) 浏览 + 播放; 极空间/群晖/Windows 共享实测通过 |

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

## "smb" (libsmb2)

SMB 走 libsmb2(LGPL, 纯 C 同步 API 跑在 RunTask 线程), 依赖预编译入库方式与 libtorrent 一致
(源项目 `D:/Work/github/libsmb2` 独立 `python build_windows.py` → 产物进
`avc_library/3rdparty/library/windows/libsmb2/`, 本仓库 `cmake/FindLibsmb2.cmake` 查找)。

```cpp
avox::IRemoteSource* src = avox::createRemoteSource("smb");
src->setOb(ob);
src->open("smb://192.168.3.20/share[/子目录]", "user", "pass", nullptr, 10000);
// onOpenResult(0) 后 list/下钻/resolve 同 dav;
src->resolve(i, nullptr);  // → smb://user:pass@host[:port]/share/path/文件.mp4
// 直链交 IMediaPlayer::open: MediaPlayer 按 smb:// 前缀自动路由 IoPlan::smb
// (IOParseSmb: libsmb2 pread → 自定义 avio → 现有解封装管线, seek 全支持)
```

要点:
- **API 陷阱**: `smb2_connect_share(ctx, server, share, user)` 无密码参, 密码须先
  `smb2_set_password`; `smb2_pread` 带显式 offset(非文件游标); `<smb2/smb2.h>` 须先于
  `<smb2/libsmb2.h>` 包含;
- **token 约定**: share 内绝对路径(`'/'`开头, 目录带尾`'/'`); open URL 带 `/子目录` 时
  顶层 `list("")`/`list("/")` 映射到该子目录;
- **guest/匿名**: open 不带 user 时以 nullptr 账号连接(游客共享);
- 中文共享名/文件名全程 UTF-8(libsmb2 与服务端 UTF-16 互转), 测试程序经 wmain 转码;
- 冒烟: `samples/functest/smbsourcetest.exe -u smb://host/share [-n user] [-p pass]
  [-m 媒体名子串] [-v]`, 覆盖 open/list 下钻/resolve/播放推进/seek, 8 项全过;
  实测环境: 极空间 Z4(SMB 服务端, 中文目录树 + 1.1GB mkv 播放/seek)与 Windows 本机共享。

## 构建

依赖: cpp-httplib(仓库内 header-only) + OpenSSL 3.0+(全局 find_package, 未找到自动跳过本插件)。
Windows 下附加 ws2_32/crypt32。产物: `<输出>/plugins/avox_remote.dll`, ModuleMgr 懒加载
(`createRemoteSource("dav")` 首调触发)。

## 测试

`script/` 外的本地冒烟: `D:\tmp\davtest\`(mock_dav.py 最小 WebDAV 服务器 + davtest.exe),
覆盖 错密码/列根/下钻/resolve 直链/目录拒解析/404 六条路径, 16 项断言全过。
