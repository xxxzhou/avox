/**
 * Zoom 功能 Mixin - 为渲染器提供缩放和拖拽能力
 */
const ZoomMixin = {
    initZoom() {
        // 幂等: WebGPU 渲染器 device 丢失重绑时会再次初始化, 不重复挂事件监听
        if (this.zoomInited) return;
        this.zoomInited = true;
        this.zoom = 1.0;
        this.panX = 0.0;
        this.panY = 0.0;
        this.isDragging = false;
        this.lastMouseX = 0;
        this.lastMouseY = 0;
        // 检测是否为离屏 canvas，离屏 canvas 无法监听 DOM 事件
        this.isOffscreen = typeof OffscreenCanvas !== 'undefined' && this.canvas instanceof OffscreenCanvas;
        if (!this.isOffscreen) {
            this.setupZoomEvents();
        }
    },

    setupZoomEvents() {
        this.wheelHandler = (e) => {
            e.preventDefault();
            e.stopPropagation();
            const delta = e.deltaY > 0 ? 0.9 : 1.1;
            const oldZoom = this.zoom;
            this.zoom = Math.max(1, Math.min(5, this.zoom * delta));

            if (this.zoom !== oldZoom) {
                const rect = this.canvas.getBoundingClientRect();
                const mouseX = (e.clientX - rect.left) / rect.width;
                const mouseY = (e.clientY - rect.top) / rect.height;

                const zoomRatio = this.zoom / oldZoom;
                this.panX = mouseX - (mouseX - this.panX) / zoomRatio;
                this.panY = mouseY - (mouseY - this.panY) / zoomRatio;
                this.clampPan();
            }
        };

        this.mouseDownHandler = (e) => {
            if (this.zoom > 1) {
                e.preventDefault();
                e.stopPropagation();
                this.isDragging = true;
                this.lastMouseX = e.clientX;
                this.lastMouseY = e.clientY;
                this.canvas.style.cursor = 'grabbing';
            }
        };

        this.mouseMoveHandler = (e) => {
            if (!this.isDragging) return;
            e.preventDefault();
            e.stopPropagation();

            const rect = this.canvas.getBoundingClientRect();
            // 鼠标移出 canvas 立即停止拖拽
            if (e.clientX < rect.left || e.clientX > rect.right ||
                e.clientY < rect.top || e.clientY > rect.bottom) {
                this.isDragging = false;
                this.canvas.style.cursor = this.zoom > 1 ? 'grab' : 'default';
                return;
            }

            const dx = (e.clientX - this.lastMouseX) / rect.width;
            const dy = (e.clientY - this.lastMouseY) / rect.height;

            this.panX -= dx / this.zoom;
            this.panY -= dy / this.zoom;
            this.clampPan();

            this.lastMouseX = e.clientX;
            this.lastMouseY = e.clientY;
        };

        this.mouseUpHandler = (e) => {
            if (this.isDragging) {
                e.preventDefault();
                e.stopPropagation();
            }
            this.isDragging = false;
            this.canvas.style.cursor = this.zoom > 1 ? 'grab' : 'default';
        };

        this.dblClickHandler = (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.zoom = 1.0;
            this.panX = 0.0;
            this.panY = 0.0;
            this.canvas.style.cursor = 'default';
        };

        this.canvas.addEventListener('wheel', this.wheelHandler, { passive: false, capture: true });
        this.canvas.addEventListener('mousedown', this.mouseDownHandler, { capture: true });
        this.canvas.addEventListener('mousemove', this.mouseMoveHandler, { capture: true });
        this.canvas.addEventListener('mouseup', this.mouseUpHandler, { capture: true });
        this.canvas.addEventListener('mouseleave', this.mouseUpHandler, { capture: true });
        this.canvas.addEventListener('dblclick', this.dblClickHandler, { capture: true });
    },

    clampPan() {
        const maxPan = (this.zoom - 1) / this.zoom;
        this.panX = Math.max(-maxPan, Math.min(maxPan, this.panX));
        this.panY = Math.max(-maxPan, Math.min(maxPan, this.panY));
    },

    setZoomPan(zoom, panX, panY) {
        this.zoom = Math.max(1, Math.min(5, zoom));
        this.panX = panX || 0;
        this.panY = panY || 0;
        this.clampPan();
        if (!this.isOffscreen && this.canvas) {
            this.canvas.style.cursor = this.zoom > 1 ? 'grab' : 'default';
        }
    },

    resetZoom() {
        this.zoom = 1.0;
        this.panX = 0.0;
        this.panY = 0.0;
        this.isDragging = false;
        if (!this.isOffscreen && this.canvas) {
            this.canvas.style.cursor = 'default';
        }
    },

    destroyZoom() {
        this.zoomInited = false;
        if (!this.isOffscreen && this.wheelHandler) {
            this.canvas.removeEventListener('wheel', this.wheelHandler, { capture: true });
            this.canvas.removeEventListener('mousedown', this.mouseDownHandler, { capture: true });
            this.canvas.removeEventListener('mousemove', this.mouseMoveHandler, { capture: true });
            this.canvas.removeEventListener('mouseup', this.mouseUpHandler, { capture: true });
            this.canvas.removeEventListener('mouseleave', this.mouseUpHandler, { capture: true });
            this.canvas.removeEventListener('dblclick', this.dblClickHandler, { capture: true });
        }
    }
};

/**
 * 计算aspect-fit居中显示区域, 对应C++的getViewRect
 * @param {number} canvasW canvas宽度
 * @param {number} canvasH canvas高度
 * @param {number} videoW 视频宽度
 * @param {number} videoH 视频高度
 * @returns {{x,y,w,h}} 居中显示区域
 */
function getViewRect(canvasW, canvasH, videoW, videoH) {
    const aspect = videoW / videoH;
    const windowAspect = canvasW / canvasH;
    let w, h;
    if (aspect > windowAspect) {
        // 视频更宽：宽度撑满，上下留黑边 (Letterboxing)
        w = canvasW;
        h = Math.round(canvasW / aspect);
    } else {
        // 视频更高：高度撑满，左右留黑边 (Pillarboxing)
        w = Math.round(canvasH * aspect);
        h = canvasH;
    }
    // 居中
    const x = Math.round((canvasW - w) / 2);
    const y = Math.round((canvasH - h) / 2);
    return { x, y, w, h };
}

// ============================================================================
// 共享 WebGPU 资源 (adapter/device/shader/pipeline/sampler)
// ----------------------------------------------------------------------------
// 以前每个 YuvWebGPURender 各自 requestDevice(): 在 Dawn/D3D12 下每路一个独立
// 设备与交换链预算, 路数一多超过 Chromium 的预算后整批被强制回收(device lost),
// 所有对象变 invalid —— 控制台刷 "Invalid RenderPipeline"/"Invalid CommandBuffer",
// 画面全黑(>9路开始异常, >=16路全黑), 与 WebGL 的活跃上下文上限是同类问题.
// 现在所有实例共享一个 device: N 路 N 个 device -> 全局只有 1 个,
// 每路只持有自己的纹理/view/bindGroup/uniform buffer.
// ============================================================================
const kMaxWebglContexts = 15;   // 浏览器 WebGL 活跃上下文上限约 16, 留 1 个余量

const kWebgpuShaderCode = `
    struct RenderParams {
        format: u32,
        stride: f32,
        vH: f32,
        zoom: f32,
        panX: f32,
        panY: f32,
        padding: vec2<f32>,
    };

    @group(0) @binding(1) var t_frame: texture_2d<f32>;
    @group(0) @binding(2) var s_frame: sampler;
    @group(0) @binding(3) var<uniform> params: RenderParams;

    struct VertexOutput {
        @builtin(position) pos: vec4<f32>,
        @location(0) uv: vec2<f32>,
    };

    @vertex
    fn vertexMain(@builtin(vertex_index) idx: u32) -> VertexOutput {
        var pos = array<vec2<f32>, 4>(
            vec2<f32>(-1.0, 1.0),  vec2<f32>(1.0, 1.0),
            vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0)
        );
        var tex = array<vec2<f32>, 4>(
            vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0),
            vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 1.0)
        );

        // 归一化 UV (含 zoom/pan), 双线性插值交给线性 sampler
        var uv = tex[idx];
        uv = (uv - 0.5) / params.zoom + 0.5;
        uv.x += params.panX;
        uv.y += params.panY;

        var out: VertexOutput;
        out.pos = vec4<f32>(pos[idx], 0.0, 1.0);
        out.uv = uv;
        return out;
    }

    @fragment
    fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
        // 边界外返回黑色
        if (input.uv.x < 0.0 || input.uv.x > 1.0 ||
            input.uv.y < 0.0 || input.uv.y > 1.0) {
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }

        let stride = params.stride;
        // Y 平面占纹理高度的 2/3 (height / (height*1.5))
        let y_uv = vec2<f32>(input.uv.x, input.uv.y * 0.666667);
        // textureSampleLevel(显式LOD): 上面的边界 if 依赖逐像素 input.uv 属于非一致控制流,
        // 规范禁止在那里用带隐式导数的 textureSample; 纹理无 mipmap, level 0 结果与原采样一致
        let y = textureSampleLevel(t_frame, s_frame, y_uv, 0.0).r - 0.0627;

        var u: f32;
        var v: f32;

        if (params.format == 1u) {
            // I420 平面: U/V 各占 1/4 高度, 色度 2:1 下采样
            let chroma_x = input.uv.x * 0.5;
            let u_uv = vec2<f32>(chroma_x, 0.666667 + input.uv.y * 0.166667);
            let v_uv = vec2<f32>(chroma_x, 0.833333 + input.uv.y * 0.166667);
            u = textureSampleLevel(t_frame, s_frame, u_uv, 0.0).r - 0.502;
            v = textureSampleLevel(t_frame, s_frame, v_uv, 0.0).r - 0.502;
        } else {
            // NV12 交织: U 在偶数列, V 在奇数列, 色度占 [0.666667, 1.0)
            let chroma_y = 0.666667 + input.uv.y * 0.333333;
            let x_px = input.uv.x * stride;
            let x_u = (floor(x_px * 0.5) * 2.0 + 0.5) / stride;
            let x_v = (floor(x_px * 0.5) * 2.0 + 1.5) / stride;
            u = textureSampleLevel(t_frame, s_frame, vec2<f32>(x_u, chroma_y), 0.0).r - 0.502;
            v = textureSampleLevel(t_frame, s_frame, vec2<f32>(x_v, chroma_y), 0.0).r - 0.502;
        }

        return vec4<f32>(
            1.164 * y + 1.793 * v,
            1.164 * y - 0.213 * u - 0.533 * v,
            1.164 * y + 2.112 * u, 1.0
        );
    }
`;

// 当前共享的 GPU 资源; null 表示尚未创建或已整体丢失(下次使用时重新申请)
let sharedGPURes = null;
// 进行中的申请 Promise, 防止并发初始化时重复 requestDevice
let sharedGPUPending = null;

// 全局验证错误去重限频: 同一条错误首条立即全量打印, 之后的重复只累计,
// 每 5 秒(或下一条不同错误到来时)汇总一行, 避免 device 时间线刷屏淹没控制台
const gpuErrAgg = { text: '', count: 0, t0: 0, timer: null };
function flushGpuErr() {
    if (gpuErrAgg.count > 1) {
        const secs = ((Date.now() - gpuErrAgg.t0) / 1000).toFixed(1);
        console.error(`==== WebGPU Validation Error (同一问题重复 x${gpuErrAgg.count}, ${secs}s) ====`);
        console.error(gpuErrAgg.text.split('\n')[0]);
        console.error('================================');
    }
    gpuErrAgg.count = 0;
}

function getSharedGPUResources() {
    if (sharedGPURes) return Promise.resolve(sharedGPURes);
    if (!sharedGPUPending) {
        const create = (async () => {
            if (!navigator.gpu) throw new Error('WebGPU not supported');
            const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
            if (!adapter) throw new Error('requestAdapter returned null');
            const device = await adapter.requestDevice();

            // 共享层单点记录验证错误与丢设备原因(以前每路各挂一遍)
            device.onuncapturederror = (event) => {
                // 未被 pushErrorScope 捕获的错误走到这里: 无 JS 栈, 无法直接定位提交者,
                // 需要结合 renderPbo/clear 里带实例编号的 errorScope 日志看
                const msg = (event.error && event.error.message) || String(event.error);
                if (msg === gpuErrAgg.text) {
                    gpuErrAgg.count++;
                } else {
                    flushGpuErr();
                    gpuErrAgg.text = msg;
                    gpuErrAgg.count = 1;
                    gpuErrAgg.t0 = Date.now();
                    console.error('==== WebGPU Validation Error ====');
                    console.error(msg);
                    console.error('================================');
                }
                if (gpuErrAgg.timer) clearTimeout(gpuErrAgg.timer);
                gpuErrAgg.timer = setTimeout(() => { gpuErrAgg.timer = null; flushGpuErr(); }, 5000);
            };
            device.lost.then((info) => {
                // 只有"当前共享 device"被回收才整体作废; 实例 destroy() 不再关 device.
                // pending 里缓存的是解析到旧 device 的 Promise, 必须一并清掉,
                // 否则丢失后重绑拿到的还是旧资源
                if (sharedGPURes && sharedGPURes.device === device) {
                    console.error('[WebGPU] 共享 device 已丢失:', info.reason || 'unknown', info.message);
                    sharedGPURes = null;
                    sharedGPUPending = null;
                }
            });

            const presentationFormat = navigator.gpu.getPreferredCanvasFormat();
            let shaderModule = device.createShaderModule({ code: kWebgpuShaderCode });
            const buildPipeline = () => device.createRenderPipeline({
                layout: 'auto',
                vertex: { module: shaderModule, entryPoint: 'vertexMain' },
                fragment: { module: shaderModule, entryPoint: 'fragmentMain', targets: [{ format: presentationFormat }] },
                primitive: { topology: 'triangle-strip' },
            });
            let pipeline = buildPipeline();
            const sampler = device.createSampler({
                magFilter: 'linear',
                minFilter: 'linear',
                addressModeU: 'clamp-to-edge',
                addressModeV: 'clamp-to-edge',
            });

            sharedGPURes = { device, presentationFormat, pipeline, sampler, version: 1, broken: false };
            console.log('[WebGPURender] 共享 device 就绪 (全局单实例)');

            // ---- 创建即自检 ----
            // WGSL 编译是异步的: 编译失败不抛异常, 只让 pipeline 静默变 invalid,
            // 之后每帧报 "SetPipeline([Invalid RenderPipeline])" 且所有 WebGPU 路黑屏.
            // 这里在创建时就验尸: 抓编译错误 + 用 RenderBundleEncoder 试一条命令.
            try {
                shaderModule.getCompilationInfo().then((info) => {
                    for (const m of info.messages) {
                        if (m.type === 'error') {
                            console.error(`[WebGPU] 着色器编译错误 @行${m.lineNum}:列${m.linePos} ${m.message}`);
                        }
                    }
                }).catch(() => {});
            } catch (_) { /* 老内核无此 API, 忽略 */ }

            const canProbe = typeof device.pushErrorScope === 'function'
                && typeof device.createRenderBundleEncoder === 'function';
            const probePipeline = () => {
                device.pushErrorScope('validation');
                const rbe = device.createRenderBundleEncoder({ colorFormats: [presentationFormat] });
                rbe.setPipeline(pipeline);
                rbe.finish();
                return device.popErrorScope();
            };
            if (canProbe) {
                probePipeline().then((err) => {
                    if (!err) {
                        console.log('[WebGPU] 共享 pipeline 自检通过');
                        return;
                    }
                    console.error('[WebGPU] 共享 pipeline 自检失败, 重建一次:', err.message);
                    shaderModule = device.createShaderModule({ code: kWebgpuShaderCode });
                    pipeline = buildPipeline();
                    sharedGPURes.pipeline = pipeline;
                    sharedGPURes.version++;
                    return probePipeline().then((err2) => {
                        if (err2) {
                            console.error('[WebGPU] 重建后仍失败, WebGPU 渲染停用, 请把上面的错误信息反馈排查:', err2.message);
                            sharedGPURes.broken = true;
                        } else {
                            console.log('[WebGPU] pipeline 重建后自检通过, 各路自动恢复');
                        }
                    });
                }).catch(() => {});
            }

            return sharedGPURes;
        })();
        sharedGPUPending = create;
        // 申请失败(无显卡/驱动问题等)时清掉 pending, 允许下一次重试
        create.catch(() => { sharedGPUPending = null; });
    }
    return sharedGPUPending;
}

// 当前存活的 WebGL 渲染器数量 (浏览器对活跃 GL 上下文有硬性上限 ~16)
let liveGLRenderCount = 0;

// WebGPU 渲染器实例计数(活着的)与累计流水号, 供日志归属与 YuvRenderStats 统计
let liveWebGPURenderCount = 0;
let webgpuInstanceSeq = 0;

function renderStats() {
    return {
        glAlive: liveGLRenderCount,
        webgpuAlive: liveWebGPURenderCount,
        webgpuCreatedTotal: webgpuInstanceSeq,
        sharedDeviceReady: !!sharedGPURes,
        pipelineBroken: !!(sharedGPURes && sharedGPURes.broken),
    };
}

class YuvWebGPURender {
    constructor(canvas) {
        this.canvas = canvas;
        this.serial = ++webgpuInstanceSeq;
        this.wgCounted = true;
        liveWebGPURenderCount++;
        this.renderType = 'none';
        this.textureFrame = null;
        this.viewFrame = null;
        this.paramsBuffer = null;
        this.bindGroup = null;
        this.initPbo = false;
        this.renderIdx = 0;
        // autoAspect: true=保持视频原始长宽比(黑边), false=拉伸填满canvas
        this.autoAspect = true;
        this.videoWidth = 0;
        this.videoHeight = 0;

        this.initWebGPU = this.initWebGPU.bind(this);
        this.initWebGPU();
    }

    // 本实例绑定的共享资源是否仍有效 (device 未被整体替换/丢失)
    isDeviceValid() {
        return !!(this.shared && sharedGPURes && sharedGPURes.device === this.shared.device);
    }

    async initWebGPU(isRecover) {
        try {
            const res = await getSharedGPUResources();
            this.shared = res;
            this.gpuDevice = res.device;

            this.gpuContext = this.canvas.getContext('webgpu');
            this.gpuContext.configure({
                device: res.device,
                format: res.presentationFormat,
                alphaMode: 'opaque',
            });

            this.paramsBuffer = this.gpuDevice.createBuffer({
                size: 32,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });

            this.renderType = 'webgpu';
            this.initZoom();   // 幂等, 重复初始化不会叠加事件监听
            console.log(`[WebGPURender] ✓ ${isRecover ? '已重新绑定共享 device' : 'System Ready'} (Row-Mode with Zoom)`);
        } catch (error) {
            console.error('[WebGPURender] ✗ Init Failed:', error.message);
            this.renderType = 'failed';
        }
    }

    // 共享 device 被整体回收后的自动重绑: 换新 device、重配置 canvas、
    // 丢弃旧 device 上的每路资源(纹理下一帧按尺寸重建).
    // 带 2s 时间门槛, 避免持续失败时形成重试风暴.
    recoverDevice() {
        if (this.recovering) return;
        const now = Date.now();
        if (this.lastRecoverAt && now - this.lastRecoverAt < 2000) return;
        this.recovering = true;
        this.lastRecoverAt = now;
        (async () => {
            try {
                this.textureFrame = null;
                this.viewFrame = null;
                this.bindGroup = null;
                // JS 帧缓冲也要换新的, native 侧下一帧会经 setRenderJsBuffer 重新登记
                this.tempBuffer = null;
                await this.initWebGPU(true);
            } finally {
                this.recovering = false;
            }
        })();
    }

    getBufferIfChanged(frameSize) {
        if (!this.tempBuffer || this.tempBuffer.length !== frameSize) {
            this.tempBuffer = new Uint8Array(frameSize);
            return this.tempBuffer;
        }
        return null;
    }

    updateTexture(width, height) {
        const totalHeight = height * 1.5;
        if (this.textureFrame && this.textureFrame.width === width && this.textureFrame.height === totalHeight) return false;

        if (this.textureFrame) this.textureFrame.destroy();
        this.textureFrame = this.gpuDevice.createTexture({
            size: [width, Math.floor(totalHeight)],
            format: 'r8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
        });
        this.viewFrame = this.textureFrame.createView();
        this.bindGroup = null;

        // 初始化纹理为黑色: Y=16, U=V=128 (对应 RGB 黑色)
        const initSize = width * Math.floor(totalHeight);
        const initData = new Uint8Array(initSize);
        // Y 平面 (前 height 行) 填充 16
        initData.fill(16, 0, width * height);
        // UV 平面 (后 height*0.5 行) 填充 128
        initData.fill(128, width * height);
        this.gpuDevice.queue.writeTexture(
            { texture: this.textureFrame },
            initData,
            { bytesPerRow: width },
            { width: width, height: Math.floor(totalHeight) }
        );
        return true;
    }

    clear() {
        // 清屏为黑色，用于换流/切换画面时调用
        if (this.renderType !== 'webgpu' || !this.gpuContext || !this.isDeviceValid()) return;
        const encoder = this.gpuDevice.createCommandEncoder();
        const pass = encoder.beginRenderPass({
            colorAttachments: [{
                view: this.gpuContext.getCurrentTexture().createView(),
                loadOp: 'clear',
                clearValue: { r: 0, g: 0, b: 0, a: 1 },
                storeOp: 'store',
            }],
        });
        pass.end();
        this.gpuDevice.queue.submit([encoder.finish()]);
    }

    resize(width, height) {
        // WebGPU: getCurrentTexture() auto-adjusts to canvas size, no-op
    }

    setAutoAspect(enable) {
        this.autoAspect = enable;
    }

    getAutoAspect() {
        return this.autoAspect;
    }

    setVideoSize(width, height) {
        this.videoWidth = width;
        this.videoHeight = height;
    }

    renderPbo(width, height, stride, format) {
        if (this.renderType !== 'webgpu' || !this.tempBuffer) return;
        if (!this.isDeviceValid()) {
            // 共享 device 被整体回收(驱动重置/预算逐出): 异步重绑后自动恢复,
            // 本帧跳过, 避免向已丢失的设备继续提交产生刷屏报错
            this.recoverDevice();
            return;
        }
        if (this.shared.broken) return;   // 创建时自检已判定 pipeline 不可用, 静默跳过
        if (this.initPbo) { this.initPbo = false; return; }

        const totalHeight = height * 1.5;
        const sizeChanged = this.updateTexture(width, height);

        if (!this.paramsData) this.paramsData = new ArrayBuffer(32);
        const u32 = new Uint32Array(this.paramsData);
        const f32 = new Float32Array(this.paramsData);
        u32[0] = format;
        f32[1] = width;  // stride 改为 width，着色器用这个做 UV 计算
        f32[2] = height;
        f32[3] = this.zoom;
        f32[4] = this.panX;
        f32[5] = this.panY;

        this.gpuDevice.queue.writeBuffer(this.paramsBuffer, 0, this.paramsData);
        this.gpuDevice.queue.writeTexture(
            { texture: this.textureFrame },
            this.tempBuffer.buffer,
            { offset: this.tempBuffer.byteOffset, bytesPerRow: stride },  // 数据行字节数
            { width: width, height: totalHeight }  // 目标区域只用 width
        );

        if (!this.bindGroup || sizeChanged || this.boundVersion !== this.shared.version) {
            this.bindGroup = this.gpuDevice.createBindGroup({
                layout: this.shared.pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 1, resource: this.viewFrame },
                    { binding: 2, resource: this.shared.sampler },
                    { binding: 3, resource: { buffer: this.paramsBuffer } },
                ],
            });
            this.boundVersion = this.shared.version;   // pipeline 被重建后 bindGroup 需同步重建
        }

        const canvasW = this.canvas.width;
        const canvasH = this.canvas.height;

        // errorScope 归属: device 时间线的验证错误不带 JS 栈, 用实例编号定位是哪一路
        const scoped = typeof this.gpuDevice.pushErrorScope === 'function';
        if (scoped) this.gpuDevice.pushErrorScope('validation');

        // WebGPU scissor/viewport: autoAspect时先clear全屏黑,再在居中区域绘制
        const encoder = this.gpuDevice.createCommandEncoder();
        const textureView = this.gpuContext.getCurrentTexture().createView();

        if (this.autoAspect && this.videoWidth > 0 && this.videoHeight > 0) {
            // 先clear全屏为黑色
            const clearPass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: textureView,
                    loadOp: 'clear',
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    storeOp: 'store',
                }],
            });
            clearPass.end();

            // 计算aspect-fit居中区域, 对应C++的getViewRect
            const vr = getViewRect(canvasW, canvasH, this.videoWidth, this.videoHeight);
            // 用scissorRect限制绘制区域为居中子区域
            const drawPass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: textureView,
                    loadOp: 'load',
                    storeOp: 'store',
                }],
            });
            drawPass.setPipeline(this.shared.pipeline);
            drawPass.setBindGroup(0, this.bindGroup);
            drawPass.setScissorRect(vr.x, vr.y, vr.w, vr.h);
            drawPass.setViewport(vr.x, vr.y, vr.w, vr.h, 0, 1);
            drawPass.draw(4);
            drawPass.end();
        } else {
            // 全屏拉伸(默认行为)
            const pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: textureView,
                    loadOp: 'clear',
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    storeOp: 'store',
                }],
            });
            pass.setPipeline(this.shared.pipeline);
            pass.setBindGroup(0, this.bindGroup);
            pass.draw(4);
            pass.end();
        }

        this.gpuDevice.queue.submit([encoder.finish()]);

        if (scoped) {
            this.gpuDevice.popErrorScope().then((err) => {
                if (!err) return;
                this.errSeq = (this.errSeq || 0) + 1;
                if (this.errSeq <= 3) {
                    console.error(`[WebGPU inst#${this.serial} ${canvasW}x${canvasH} 第${this.errSeq}次] `, err.message);
                } else if (this.errSeq === 4) {
                    console.error(`[WebGPU inst#${this.serial}] 同类提交错误已抑制(每实例只报前3次)`);
                }
            }).catch(() => {});
        }
    }

    destroy() {
        this.destroyZoom();
        if (this.wgCounted) {
            liveWebGPURenderCount--;
            this.wgCounted = false;
        }
        // device/pipeline/sampler 是全局共享的, 不能在这里销毁;
        // 每路只释放自己的纹理和 uniform buffer
        if (this.textureFrame) {
            this.textureFrame.destroy();
            this.textureFrame = null;
        }
        if (this.paramsBuffer) {
            this.paramsBuffer.destroy();
            this.paramsBuffer = null;
        }
        this.gpuContext = null;
        this.gpuDevice = null;
        this.shared = null;
        this.renderType = 'destroyed';
    }
}

Object.assign(YuvWebGPURender.prototype, ZoomMixin);

class YuvGLRender {
    // 当前存活的 WebGL 渲染器数量(供 preload 按 ~16 个的浏览器上限选择后端)
    static aliveContexts() { return liveGLRenderCount; }

    constructor(canvas) {
        this.canvas = canvas;
        this.gl = null;
        this.program = null;
        this.texture = null;
        this.renderType = 'none';
        this.lastMeta = { width: 0, height: 0, format: -1 };
        this.tempBuffer = null;
        // autoAspect: true=保持视频原始长宽比(黑边), false=拉伸填满canvas
        this.autoAspect = true;
        this.videoWidth = 0;
        this.videoHeight = 0;
        this.initWebGL();
    }

    getBufferIfChanged(frameSize) {
        if (!this.tempBuffer || this.tempBuffer.length !== frameSize) {
            this.tempBuffer = new Uint8Array(frameSize);
            return this.tempBuffer;
        }
        return null;
    }

    initWebGL() {
        const gl = this.canvas.getContext('webgl2', {
            antialias: false,
            depth: false,
            alpha: false,
            powerPreference: 'high-performance'
        });

        if (!gl) {
            this.renderType = 'failed';
            return;
        }
        this.gl = gl;

        // 浏览器超限时按 LRU 强制丢弃最旧 GL 上下文, 监听丢失事件同步递减计数
        this.ctxLostHandler = () => {
            if (this.glCounted) {
                liveGLRenderCount--;
                this.glCounted = false;
            }
        };
        this.canvas.addEventListener('webglcontextlost', this.ctxLostHandler);

        const vs = `#version 300 es
            layout(location = 0) in vec2 pos;
            out vec2 v_uv_y;
            out vec2 v_uv_u;
            out vec2 v_uv_v;
            uniform highp int u_format;
            uniform float u_zoom;
            uniform vec2 u_pan;

            void main() {
                vec2 uv = pos * 0.5 + 0.5;
                uv.y = 1.0 - uv.y;
                uv = (uv - 0.5) / u_zoom + 0.5;
                uv.x += u_pan.x;
                uv.y += u_pan.y;

                // 不在顶点着色器做边界判断，避免 GPU 插值错误
                v_uv_y = vec2(uv.x, uv.y * 0.666667);
                if (u_format == 1) {
                    v_uv_u = vec2(uv.x * 0.5, 0.666667 + uv.y * 0.166667);
                    v_uv_v = vec2(uv.x * 0.5, 0.833333 + uv.y * 0.166667);
                } else {
                    v_uv_u = vec2(uv.x, 0.666667 + uv.y * 0.333333);
                    v_uv_v = v_uv_u;
                }
                gl_Position = vec4(pos, 0.0, 1.0);
            }`;

        const fs = `#version 300 es
            precision highp float;
            precision highp int;
            uniform sampler2D t_frame;
            uniform int u_format;
            uniform float u_stride;
            in vec2 v_uv_y;
            in vec2 v_uv_u;
            in vec2 v_uv_v;
            out vec4 fragColor;

            void main() {
                // 完整边界检查：Y 平面在纹理的 0~0.666667 区域
                if (v_uv_y.x < 0.0 || v_uv_y.x > 1.0 ||
                    v_uv_y.y < 0.0 || v_uv_y.y > 0.666667) {
                    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    return;
                }

                float y = texture(t_frame, v_uv_y).r - 0.0627;
                float u, v;
                if (u_format == 0) {
                    float x_px = v_uv_u.x * u_stride;
                    u = texture(t_frame, vec2((floor(x_px * 0.5) * 2.0 + 0.5) / u_stride, v_uv_u.y)).r - 0.502;
                    v = texture(t_frame, vec2((floor(x_px * 0.5) * 2.0 + 1.5) / u_stride, v_uv_u.y)).r - 0.502;
                } else {
                    u = texture(t_frame, v_uv_u).r - 0.502;
                    v = texture(t_frame, v_uv_v).r - 0.502;
                }
                fragColor = vec4(
                    1.164 * y + 1.596 * v,
                    1.164 * y - 0.391 * u - 0.813 * v,
                    1.164 * y + 2.018 * u, 1.0
                );
            }`;

        this.program = this.createProgram(gl, vs, fs);
        if (!this.program) { this.renderType = 'failed'; return; }

        this.locations = {
            u_format: gl.getUniformLocation(this.program, 'u_format'),
            u_stride: gl.getUniformLocation(this.program, 'u_stride'),
            t_frame: gl.getUniformLocation(this.program, 't_frame'),
            u_zoom: gl.getUniformLocation(this.program, 'u_zoom'),
            u_pan: gl.getUniformLocation(this.program, 'u_pan')
        };

        const vao = gl.createVertexArray();
        gl.bindVertexArray(vao);
        const buf = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buf);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, 1, 1, 1, -1, -1, 1, -1]), gl.STATIC_DRAW);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

        this.renderType = 'webgl2';
        this.glCounted = true;
        liveGLRenderCount++;
        this.initZoom();
    }

    updateTexture(width, height) {
        const gl = this.gl;
        if (this.lastMeta.width === width && this.lastMeta.height === height) return;

        const totalHeight = Math.floor(height * 1.5);
        if (!this.texture) this.texture = gl.createTexture();

        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);

        // 初始化纹理为黑色: Y=16, U=V=128 (对应 RGB 黑色)
        const initData = new Uint8Array(width * totalHeight);
        initData.fill(16, 0, width * height);
        initData.fill(128, width * height);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, width, totalHeight, 0, gl.RED, gl.UNSIGNED_BYTE, initData);

        this.lastMeta.width = width;
        this.lastMeta.height = height;
    }

    clear() {
        // 清屏为黑色，用于换流/切换画面时调用
        const gl = this.gl;
        if (!gl) return;
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
    }

    resize(width, height) {
        const gl = this.gl;
        if (!gl) return;
        gl.viewport(0, 0, width, height);
    }

    setAutoAspect(enable) {
        this.autoAspect = enable;
    }

    getAutoAspect() {
        return this.autoAspect;
    }

    setVideoSize(width, height) {
        this.videoWidth = width;
        this.videoHeight = height;
    }

    renderPbo(width, height, stride, format) {
        const gl = this.gl;
        if (!gl || !this.program || !this.tempBuffer) return;

        this.updateTexture(width, height);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, stride);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, Math.floor(height * 1.5), gl.RED, gl.UNSIGNED_BYTE, this.tempBuffer);

        // autoAspect: 先clear黑色全屏,再设viewport到居中区域绘制
        // 对应C++的getViewRect + RSSetViewports逻辑
        if (this.autoAspect && this.videoWidth > 0 && this.videoHeight > 0) {
            gl.clearColor(0, 0, 0, 1);
            gl.clear(gl.COLOR_BUFFER_BIT);
            const canvasW = this.canvas.width;
            const canvasH = this.canvas.height;
            const vr = getViewRect(canvasW, canvasH, this.videoWidth, this.videoHeight);
            gl.viewport(vr.x, vr.y, vr.w, vr.h);
        } else {
            // 全屏拉伸
            gl.viewport(0, 0, this.canvas.width, this.canvas.height);
        }

        gl.useProgram(this.program);
        gl.uniform1i(this.locations.u_format, format);
        gl.uniform1f(this.locations.u_stride, parseFloat(width));  // 纹理宽度是 width
        gl.uniform1i(this.locations.t_frame, 0);
        gl.uniform1f(this.locations.u_zoom, this.zoom);
        gl.uniform2f(this.locations.u_pan, this.panX, this.panY);

        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    createProgram(gl, vsSource, fsSource) {
        const loadShader = (type, source) => {
            const shader = gl.createShader(type);
            gl.shaderSource(shader, source.trim());
            gl.compileShader(shader);
            if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) return null;
            return shader;
        };
        const vs = loadShader(gl.VERTEX_SHADER, vsSource);
        const fs = loadShader(gl.FRAGMENT_SHADER, fsSource);
        const program = gl.createProgram();
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        return gl.getProgramParameter(program, gl.LINK_STATUS) ? program : null;
    }

    destroy() {
        this.destroyZoom();
        if (this.glCounted) {
            liveGLRenderCount--;
            this.glCounted = false;
        }
        if (this.canvas && this.ctxLostHandler) {
            this.canvas.removeEventListener('webglcontextlost', this.ctxLostHandler);
            this.ctxLostHandler = null;
        }
        const gl = this.gl;
        if (gl) {
            if (this.texture) gl.deleteTexture(this.texture);
            if (this.program) gl.deleteProgram(this.program);
            // 主动丢失 context,释放底层 ANGLE/D3D 资源(多路切换防 context 耗尽)
            const loseCtx = gl.getExtension('WEBGL_lose_context');
            if (loseCtx) loseCtx.loseContext();
        }
        this.gl = null;
        this.renderType = 'destroyed';
    }
}

Object.assign(YuvGLRender.prototype, ZoomMixin);

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { YuvWebGPURender, YuvGLRender, kMaxWebglContexts, renderStats };
}

if (typeof window !== 'undefined') {
    window.YuvWebGPURender = YuvWebGPURender;
    window.YuvGLRender = YuvGLRender;
    window.kMaxWebglContexts = kMaxWebglContexts;
    // 诊断: 控制台里随时调 YuvRenderStats() 看当前渲染器构成
    window.YuvRenderStats = renderStats;
}
