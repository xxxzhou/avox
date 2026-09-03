# Nodejs

[FindNode](https://github.com/mltframework/mlt/blob/master/cmake/FindNode.cmake)

cmake在这只生成commonJAVASCRIPT_wrap.cxx文件，编译使用node-gyp，其会自动把当前环境下的头文件与lib包含进去。

## 环境

1. 安装 Node.js（推荐 18 LTS 或 20 LTS）。可以用 nvm：
   ```
   nvm install 20.18
   nvm use 20.18
   ```
   也可以直接装官方包 https://nodejs.org/。

2. **不要全局装任何东西**（`node-gyp` / `electron` / `electron-builder` 都不装全局）。全局多份容易和工程版本打架。本工程所有依赖都装在 `swig/nodejs/node_modules/`，进入项目目录跑一次：
   ```
   cd swig/nodejs
   npm install
   ```
   `node-gyp` 在 `dependencies`，`electron` / `electron-builder` / `electron-rebuild` / `node-addon-api` 在 `devDependencies`，一次安装全搞定。

3. 首次 `npm install` 时，node-gyp 会自动下载对应 Node 版本的 v8 头文件，缓存到 `%LOCALAPPDATA%\node-gyp\Cache\<version>`。

4. 后续编译 / 运行 / 打包都走 `npm run` 或 `npx`，自动从本地 `node_modules/.bin/` 找——**不会**用到全局的同名工具。

## Electron

### 编译

依赖已在 `package.json` 的 `devDependencies` 里，`npm install` 时自动装好，**不要全局装**。

```bash
cd swig/nodejs

# 生成 native 项目
npm run configure

# Electron 必须用 Electron 的 node-abi 编译，不能用本机 Node 的头文件
# 默认 release（绑 avox.dll 的 Release 构建）：
npm run rebuild -- --target=v22.3.18 --arch=x64 --dist-url=https://electronjs.org/headers

# debug 构建（带调试符号，便于排查原生崩溃；绑 avox.dll 的 Debug 构建）：
npm run rebuild:debug -- --target=v22.3.18 --arch=x64 --dist-url=https://electronjs.org/headers
```

`--target=` 后的版本号必须和 `package.json` 的 `electron` 版本一致（当前 `^22.3.18`）。升级 electron 后必须重新 rebuild，否则 `electron main.js` 报 node 版本不匹配。

构建配置必须和当前 `avox.dll` 一致（`binding.gyp` 里 Debug 链 `install/AMD64/Debug/avox.lib`，Release 链 `install/AMD64/Release/avox.lib`），否则链接期符号对不上。切换 avox.dll 配置后也要重 rebuild。

如果提示 `fatal error LNK1103: 调试信息损坏或丢失`，删 `build/<Debug|Release>/avox_js.ipdb` / `avox_js.iobj`。

### 运行

```bash
npm start
```

`scripts.start = "electron main.js"`，npm 自动从 `node_modules/.bin/electron.cmd` 找本地版本。

## 打包

`electron-builder` 已在 `devDependencies`，无需额外安装：

```bash
npm run dist          # 产出 NSIS 安装包
npm run dist:dir      # 产出未打包目录（调试用）
```

打包配置见 `package.json` 的 `build` 字段。

## 注意

回调: Swig不支持nodejs对回调函数的封装，因此需要手动封装，在文件夹nativeOb是对当前项目回调类的封装。

nodejs封装的C++类,不能出现重载方法,相应方法无效.

**所有工具走本地**：node-gyp / electron / electron-builder / electron-rebuild 一律装在 `swig/nodejs/node_modules/`，编译用 `npm run configure` / `npm run rebuild -- ...`，运行用 `npm start`，打包用 `npm run dist`。**不要全局装**，全局版本和工程 `package.json` 里锁的版本脱钩时会编译失败或运行报 node 版本不匹配。若已经全局装了某个包，先 `npm uninstall -g <pkg>` 再 `npm install` 本地版本。

## ONVIF 对讲

RTSP 对讲有二种模式：普通推流和 ONVIF Backchannel。同类型设备一般用同一模式。如果不确定设备使用什么模式, 可用 `checkOnvif()` 探测设备是否支持 ONVIF。注意：URL 中的 token 可能单次有效，探测后会消耗 token，需用重新拉新对讲地址再对讲。ONVIF 模式下编码格式由设备 SDP 决定，如果不确定，用 `checkOnvif()` 返回的音频参数即可。

## CPU渲染

原native window的方式因和网页上的多层dom叠加层次显示不对,转成CPU然后提交到网页渲染.

在经历把swig从v8改成napi后,v8限定C++/JS的处理只能在主进程,而渲染到网页必需在渲染进程上,所以改napi,然后所有回调类全改成napi,这样渲染进程在preload.js能调用相应C++的接口与回调.webgl环境在渲染页面一直报错,相应提示说是webgl环境没问题,但是webgl上下文拿不到,全改webgpu.

原生窗口+vulkan/dx11渲染,CPU占用0.2%左右.在electron中,这种转到CPU然后交给node,再到网页用webgpu把CPU的数据提交到GPU渲染的方式,其CPU平均占用7%左右,尝试如下几次优化,但是效果都不太好.
1. 想优化掉CPU帧Copy,C++回调里把指针通过Napi::ArrayBuffer::New封装,提示[DEP0168] DeprecationWarning: Uncaught N-API callback exception detected, please run node with option --force-node-api-uncaught-exceptions-policy=true to handle those exceptions properly.还是只能Napi::Buffer<uint8_t>::Copy复制一次.
2. 想让回调的帧数据直接放到渲染页面,因为把回调对象转移到渲染js中,然后把js对象传入preload.js,让C++封装,但是发现js对象从渲染进程传入preload.js会丢失
3. 尝试preload.js里直接申请JS内存,每帧数据来到时,直接把数据copy到JS内存中,这种方式实现了,但是CPU占用更高9%,则查找资料,发现是preload.js里的buffer到渲染进程页面时,还复制了一次,虽然preload.js也在渲染进程,但是构架上是隔离的,除非contextIsolation: false,但是这样有安全风险,用法也完全不同,考虑通用,放弃.
4. AI给的SharedArrayBuffer方案,但是测试发现,不管是从preload.js到渲染js,还是渲染js到preload.js,要么提示DEP0168,要么就是An object could not be cloned,后面发现,不只是SharedArrayBuffer,连Uint8Array也不行,所以放弃.
5. 继续尝试AI给的window.postMessage,发现确实可以把SharedArrayBuffer的句柄从preload.js发送到渲染js,发现在渲染js里能打印出SharedArrayBuffer,但是调用WebGPU渲染时,又提示DEP0168了,奇怪了,明明每帧打印都正常.经catch,发现TypeError: Failed to execute 'writeTexture' on 'GPUQueue': parameter 2 is not of type 'ArrayBuffer',只能再复制一次成ArrayBuffer,丢给WebGPU渲染,CPU占用6%,低了些.
6. 现在问题在于C++只能与preload.js交互,而preload.js与渲染js虽然都是渲染进程中,但是架构上是隔离的,因此换种思路,能否让渲染js把页面的canvas转移给preload.js控制,此前翻看资料都说preload.js是不可以控制页面dom的,但是这话不完全,preload.js是不能操作UI,经测试,可以让页面的canvas转移离屏渲染,然后让window.postMessage把离屏后的canvas传给preload.js,preload.js拿到canvas后,再让WebGPU渲染,但是在canvas离屏渲染转移控制权后,渲染js控制canvas需要特殊的方式,并且渲染js不能再canvas.getContext绘图.CPU占用4%.

## Electron 外部GPU纹理导入

[import external shared texture into VideoFrame](https://github.com/electron/electron/pull/46811)

[0017-shared-texture.md](https://github.com/reitowo/rfcs/blob/main-shared-texture/text/0017-shared-texture.md)

## 交互渲染

[MirSurfaceKHR](https://github.com/codepilot/vulkan/blob/master/autogen/MirSurfaceKHR.cpp)

[实现steam game overlay(在electron环境下)](https://zhuanlan.zhihu.com/p/45438134)

[把原生窗口嵌入到Electron BrowserWindow内](https://zhuanlan.zhihu.com/p/620300579)

[SWIG Native C++ / NodeJS Callbacks · GitHub](https://gist.github.com/TekuConcept/e92d10d6585c9d1fd3d507d1918cf77b)

``` js
// 1. 请求适配器和设备
const adapter = await navigator.gpu.requestAdapter({
    powerPreference: 'high-performance',
});
const device = await adapter.requestDevice();

// 2. 从 Native 模块获取共享句柄（需自行实现）
const sharedHandle = await window.nativeModule.getDX11SharedHandle();

// 3. 尝试导入外部纹理（实验性）
const externalTexture = device.importExternalTexture({
    source: sharedHandle,
    colorSpace: 'srgb',
    format: 'rgba8unorm',
});
```
