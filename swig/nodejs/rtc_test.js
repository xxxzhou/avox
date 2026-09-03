// =================================== 全局变量 ===================================
let peerConnection = null;
let localStream = null;
let sseConnection = null;

// 候选者相关
let localCandidateCount = 0;
let remoteCandidateCount = 0;
let localCandidatesList = [];
let remoteCandidatesList = [];

// 候选者发送队列
const candidateSendConfig = {
    maxConcurrent: 2,
    delayBetween: 100,
    maxRetries: 0,  //重试次数
    retryDelay: 500
};
let candidateQueue = [];
let activeSendCount = 0;
let sendingPaused = false;

// 对端候选者缓存（关键）
let pendingRemoteCandidates = [];
let isRemoteDescriptionSet = false;

// ================================= 工具函数 ===================================

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function getTime() {
    const now = new Date();
    return now.toLocaleTimeString('zh-CN', { hour12: false }) + '.' +
        String(now.getMilliseconds()).padStart(3, '0');
}

function log(type, message, detail = null) {
    const container = document.getElementById('logContent');
    const entry = document.createElement('div');
    entry.className = `log-entry log-${type}`;

    const tags = {
        'info': ['tag-info', 'INFO'],
        'send': ['tag-send', 'SEND'],
        'receive': ['tag-recv', 'RECV'],
        'success': ['tag-ok', 'OK'],
        'error': ['tag-err', 'ERR'],
        'local': ['tag-local', 'LOCAL'],
        'remote': ['tag-remote', 'REMOTE'],
        'selected': ['tag-selected', 'SELECTED']
    };
    const [tagClass, tagText] = tags[type] || tags['info'];

    let html = `<span class="log-time">[${getTime()}]</span>`;
    html += `<span class="log-tag ${tagClass}">${tagText}</span>`;
    html += message;
    if (detail) {
        html += `<br><small style="color:#888;margin-left:80px;">${detail}</small>`;
    }

    entry.innerHTML = html;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;
    console.log(`[${tagText}]`, message, detail || '');
}

// 解析候选者字符串，提取关键信息
function parseCandidateString(candidateStr) {
    const parts = candidateStr.split(' ');
    const info = {
        foundation: parts[0]?.replace('candidate:', ''),
        component: parts[1],
        protocol: parts[2],
        priority: parts[3],
        ip: parts[4],
        port: parts[5],
        type: 'unknown'
    };

    const typIndex = parts.indexOf('typ');
    if (typIndex !== -1 && parts[typIndex + 1]) {
        info.type = parts[typIndex + 1];
    }

    // 获取 raddr 和 rport (对于 srflx 和 relay)
    const raddrIndex = parts.indexOf('raddr');
    if (raddrIndex !== -1 && parts[raddrIndex + 1]) {
        info.relatedAddress = parts[raddrIndex + 1];
    }
    const rportIndex = parts.indexOf('rport');
    if (rportIndex !== -1 && parts[rportIndex + 1]) {
        info.relatedPort = parts[rportIndex + 1];
    }

    return info;
}

// 格式化候选者信息为HTML
function formatCandidateDetail(info, index) {
    const typeClass = `type-${info.type}`;
    return `<div class="candidate-detail">
        <span>#${index}</span>
        <span class="${typeClass}"><b>${info.type.toUpperCase()}</b></span>
        <span>${info.protocol.toUpperCase()}</span>
        <span>${info.ip}:${info.port}</span>
        <span>优先级: ${info.priority}</span>
        ${info.relatedAddress ? `<span>中继: ${info.relatedAddress}:${info.relatedPort}</span>` : ''}
    </div>`;
}

// 日志带候选者详情
function logCandidate(direction, candidateStr, index) {
    const info = parseCandidateString(candidateStr);
    const container = document.getElementById('logContent');
    const entry = document.createElement('div');

    const isLocal = direction === 'local';
    entry.className = `log-entry log-candidate`;

    let html = `<span class="log-time">[${getTime()}]</span>`;
    html += `<span class="log-tag ${isLocal ? 'tag-local' : 'tag-remote'}">${isLocal ? '本端候选' : '对端候选'}</span>`;
    html += `收集到 ${info.type.toUpperCase()} 候选者 #${index}`;
    html += formatCandidateDetail(info, index);

    entry.innerHTML = html;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;

    console.log(`[${isLocal ? 'LOCAL' : 'REMOTE'}]`, info);
}

function clearLog() {
    document.getElementById('logContent').innerHTML = '';
}

function updateStatus(id, status, text) {
    const badge = document.getElementById(id);
    badge.className = `status-badge status-${status}`;
    badge.textContent = text;
}

function updateCandidateCount() {
    document.getElementById('localCandCount').textContent = localCandidateCount;
    document.getElementById('remoteCandCount').textContent = remoteCandidateCount;

    document.getElementById('localCandCount').className =
        `status-badge ${localCandidateCount > 0 ? 'status-connected' : 'status-disconnected'}`;
    document.getElementById('remoteCandCount').className =
        `status-badge ${remoteCandidateCount > 0 ? 'status-connected' : 'status-disconnected'}`;
}

function getConfig() {
    return {
        signalServer: document.getElementById('signalServer').value.replace(/\/$/, ''),
        token: document.getElementById('token').value.trim(),
        selfId: document.getElementById('selfId').value.trim(),
        turnServer: document.getElementById('turnServer').value,
        turnUsername: document.getElementById('turnUsername').value,
        turnPassword: document.getElementById('turnPassword').value
    };
}

function disableButtons() {
    document.getElementById('btnOffer').disabled = true;
    document.getElementById('btnAnswer').disabled = true;
}

function enableButtons() {
    document.getElementById('btnOffer').disabled = false;
    document.getElementById('btnAnswer').disabled = false;
}

// ============================================ SSE 连接 ============================================
function connectSSE() {
    return new Promise((resolve, reject) => {
        const config = getConfig();

        if (!config.token || !config.selfId) {
            reject(new Error('请填写 Token 和 Self ID'));
            return;
        }

        const sseUrl = `${config.signalServer}?token=${encodeURIComponent(config.token)}&self_id=${encodeURIComponent(config.selfId)}&method=sse&timeout=100000`;

        log('info', '建立 SSE 连接...', sseUrl);
        updateStatus('sseStatus', 'connecting', '连接中');

        sseConnection = new EventSource(sseUrl);

        sseConnection.onopen = () => {
            log('success', 'SSE 连接成功');
            updateStatus('sseStatus', 'connected', '已连接');
            resolve();
        };

        sseConnection.onmessage = (event) => {
            log('receive', '收到消息', event.data);
            // log('receive', '收到消息', event.data.substring(0, 600));
            handleSSEMessage(event.data);
        };

        sseConnection.onerror = () => {
            log('error', 'SSE 连接失败');
            updateStatus('sseStatus', 'disconnected', '已断开');
            reject(new Error('SSE 连接失败'));
        };

        setTimeout(() => {
            if (sseConnection.readyState === EventSource.CONNECTING) {
                sseConnection.close();
                reject(new Error('SSE 连接超时'));
            }
        }, 10000);
    });
}

// ================================================= 处理 SSE 消息 ================================================================
async function handleSSEMessage(data) {
    try {
        const message = JSON.parse(data);
        const method = message.method || message.type;

        // 忽略连接确认消息
        if (message.code === 200 && message.msg === 'connected') {
            log('success', 'SSE 连接确认');
            return;
        }    
        if(rtcPlayer == null) {
            return;
        }

        // ========== 新增：从 data 层获取数据（兼容旧格式）==========
        const messageData = message.data || message;

        if (method === 'offer') {            
            log('info', '收到 Offer, 设置对端描述...');
            // 标记对端描述未设置
            isRemoteDescriptionSet = false;                    
            rtcPlayer.setRemoteSdp(messageData.sdp);
            // 标记对端描述已设置
            isRemoteDescriptionSet = true;
            log('success', '对端描述设置成功');
        } else if (method === 'answer') {
            log('info', '收到 Answer, 设置对端描述...');
            // 标记对端描述未设置
            isRemoteDescriptionSet = false;           
            rtcPlayer.setRemoteSdp(messageData.sdp);
            // 标记对端描述已设置
            isRemoteDescriptionSet = true;
            log('success', '对端描述设置成功');
        } else if (method === 'candidate') {
            // 从 messageData 获取候选者字符串
            let candidateString = messageData.candidate;
            if (!candidateString) {
                log('info', '空候选者，忽略');
                return;
            }
            // 如果是字符串，需要构造完整的候选者对象
            let candidateData;
            if (typeof candidateString === 'string') {
                candidateData = {
                    candidate: candidateString,
                    sdpMid: messageData.sdpMid || '0',
                    sdpMLineIndex: messageData.sdpMLineIndex !== undefined ? messageData.sdpMLineIndex : 0
                };
            } else {
                // 如果已经是对象
                candidateData = candidateString;
            }

            // 添加调试日志
            log('info', `候选者详情: sdpMid=${candidateData.sdpMid}, sdpMLineIndex=${candidateData.sdpMLineIndex}`);

            // 记录对端候选者
            remoteCandidateCount++;
            remoteCandidatesList.push(candidateData);
            updateCandidateCount();

            if (candidateData.candidate) {
                logCandidate('remote', candidateData.candidate, remoteCandidateCount);
            }
            rtcPlayer.addIceCandidate(candidateData.candidate, candidateData.sdpMid, candidateData.sdpMLineIndex);
        }
    } catch (error) {
        log('error', `处理消息失败: ${error.message}`);
    }
}

// ================== 发送信令（带重试）====================
async function sendSignal(method, data, retryCount = 0) {
    const config = getConfig();

    const url = `${config.signalServer}?token=${encodeURIComponent(config.token)}&self_id=${encodeURIComponent(config.selfId)}&method=${method}&timeout=30000`;

    const body = {
        event: 'webrtc.live',
        method: method,
        stream_id: 'stream000000000000',
        version: 1,
        ...data
    };

    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 10000);

        const response = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
            signal: controller.signal
        });

        clearTimeout(timeoutId);

        const text = await response.text();

        if (!text || text.trim() === '') {
            return { code: 0 };
        }

        try {
            const result = JSON.parse(text);
            if (result.code === 0 || result.code === 200) {
                return result;
            } else {
                throw new Error(result.msg || '发送失败');
            }
        } catch (parseError) {
            if (response.ok) {
                return { code: 0 };
            }
            throw new Error(`服务器响应: ${text.substring(0, 100)}`);
        }

    }
    catch (error) {
        if (retryCount < candidateSendConfig.maxRetries) {
            const isTimeout = error.name === 'AbortError' || 
                            error.message.includes('Timeout');

            if (isTimeout || error.message.includes('Failed to fetch')) {
                log('info', `${method} 超时，重试中 (${retryCount + 1}/${candidateSendConfig.maxRetries})`);
                await sleep(candidateSendConfig.retryDelay * (retryCount + 1));
                return sendSignal(method, data, retryCount + 1);
            }
        }
        throw error;
    }
}

// ======================================== 候选者发送队列 ==================================================
function queueCandidateForSending(candidateJson, count) {
    candidateQueue.push({ candidateJson, count, retries: 0 });
    processCandidateQueue();
}

async function processCandidateQueue() {
    while (candidateQueue.length > 0 &&
        activeSendCount < candidateSendConfig.maxConcurrent &&
        !sendingPaused) {

        const item = candidateQueue.shift();
        activeSendCount++;

        sendSingleCandidate(item).finally(() => {
            activeSendCount--;
            setTimeout(processCandidateQueue, candidateSendConfig.delayBetween);
        });

        await sleep(candidateSendConfig.delayBetween);
    }
}

async function sendSingleCandidate(item) {
    const { candidateJson, count } = item;
    const info = parseCandidateString(candidateJson.candidate);

    try {
        log('send', `发送候选者 #${count}: ${info.type.toUpperCase()} ${info.protocol} ${info.ip}:${info.port}`);

        // await sendSignal('candidate', { 
        //     candidate: JSON.stringify(candidateJson) 
        // });

        //await sendSignal('candidate', candidateJson);

        await sendSignal('candidate', {
            candidate: candidateJson.candidate,
            sdpMid: candidateJson.sdpMid,
            sdpMLineIndex: candidateJson.sdpMLineIndex,
            usernameFragment: candidateJson.usernameFragment
        });

    }
    catch (error) {
        log('error', `候选者 #${count} 发送失败: ${error.message}`);

        if (item.retries < candidateSendConfig.maxRetries) {
            item.retries++;
            log('info', `候选者 #${count} 将重试 (${item.retries}/${candidateSendConfig.maxRetries})`);
            candidateQueue.push(item);
        }
    }
}

// ==================== 全局变量：用于存储原始数据 ====================
let rawDataBuffer = [];
let isRawRecording = false;

// ====================================== 创建 PeerConnection ======================================
function createPeerConnection() {
    const config = getConfig();

    const rtcConfig = {
        iceServers: [
            { urls: 'stun:stun.l.google.com:19302' },
            {
                urls: config.turnServer,
                username: config.turnUsername,
                credential: config.turnPassword
            }
        ],
        iceCandidatePoolSize: 0,

        // 添加这一行，强制只使用 relay
        //iceTransportPolicy: 'relay'
    };

    log('info', '创建 PeerConnection (Trickle ICE 模式)');
    //log('info', '创建 PeerConnection (强制 RELAY 模式)');

    // peerConnection = new RTCPeerConnection(rtcConfig);

    // ================== 根据模式决定方向 ==================
    if (recvOnlyMode) {
        log('info', 'PeerConnection 使用 recvonly 模式');
        // peerConnection.addTransceiver('video', { direction: 'recvonly' });
        // peerConnection.addTransceiver('audio', { direction: 'recvonly' });
    }

    // 重置所有状态
    candidateQueue = [];
    activeSendCount = 0;
    sendingPaused = false;
    localCandidateCount = 0;
    remoteCandidateCount = 0;
    localCandidatesList = [];
    remoteCandidatesList = [];
    pendingRemoteCandidates = [];  // 重置缓存
    isRemoteDescriptionSet = false;  // 重置标记
    updateCandidateCount();   
}


// ====================================== 获取本端媒体 ======================================
async function getLocalMedia() {
    try {
        log('info', '获取本端媒体...');
        localStream = await navigator.mediaDevices.getUserMedia({
            video: true,
            audio: true
        });

        localStream.getTracks().forEach(track => {
            peerConnection.addTrack(track, localStream);
        });

        document.getElementById('localVideo').srcObject = localStream;
        log('success', '本端媒体获取成功');
    } catch (error) {
        log('info', '无摄像头，使用 mp4 作为视频源');

        await useMp4AsStream('./test.mp4'); // mp4 路径
    }
}

async function useMp4AsStream(url) {
    const video = document.getElementById('fileVideo');
    video.src = url;
    video.loop = true;
    video.muted = true;
    video.playsInline = true;
    video.crossOrigin = 'anonymous';

    await video.play();

    let stream = null;

    // ===== Chrome / Edge / Firefox =====
    if (typeof video.captureStream === 'function') {
        log('info', '使用 video.captureStream()');
        stream = video.captureStream();

    } else if (typeof video.mozCaptureStream === 'function') {
        log('info', '使用 video.mozCaptureStream()');
        stream = video.mozCaptureStream();

        // ===== Safari / iOS / WebView =====
    } else {
        log('info', 'captureStream 不支持，使用 Canvas 方案');

        const canvas = document.createElement('canvas');
        canvas.width = video.videoWidth || 640;
        canvas.height = video.videoHeight || 480;

        const ctx = canvas.getContext('2d');

        function draw() {
            if (!video.paused && !video.ended) {
                ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
            }
            requestAnimationFrame(draw);
        }
        draw();

        stream = canvas.captureStream(25); // Safari OK
    }

    // ===== 兜底保护（极端情况）=====
    if (!stream) {
        throw new Error('无法从 MP4 创建 MediaStream');
    }

    // ===== 统一添加 Track =====
    stream.getTracks().forEach(track => {
        peerConnection.addTrack(track, stream);
    });

    document.getElementById('localVideo').srcObject = stream;
    localStream = stream;

    log('success', '使用 MP4 作为 WebRTC 视频源');
}

// ====================================== Offer 方流程 ======================================
async function startAsOffer() {
    disableButtons();
    log('info', '========== 作为 Offer 方开始 ==========');

    try {
        const canvas = document.getElementById('videoCanvas');
        rtcPlayer = new WebRtcPlayer(canvas);
        rtcPlayer.setRollType(false);
        rtcPlayer.onLocalSdp((localSdp) => {
            log('info', '发送offer');
            sendSignal('offer', { sdp: localSdp });
            log('success', 'offer已发送');
        });
        rtcPlayer.onIceCandidate((candidate, mid, lineIndex) => {
            sendSignal('candidate', {
                candidate: candidate,
                sdpMid: mid,
                sdpMLineIndex: lineIndex
            });
        })
        rtcPlayer.open();

        if (!recvOnlyMode) {
            await getLocalMedia(); // 原本流程
        } else {
            log('info', 'recvonly 模式：跳过本地媒体采集');
        }

        await connectSSE();

        // log('info', '创建 Offer...');
        // const offer = await peerConnection.createOffer();
        // await peerConnection.setLocalDescription(offer);
        // log('success', 'Offer 创建成功');
        // log("info", offer.sdp );
        // await sendSignal('offer', { sdp: offer.sdp });

        // log('info', '等待 Answer 和候选者收集...');

    } catch (error) {
        log('error', `Offer 流程失败: ${error.message}`);
        enableButtons();
    }
}

// ====================================== Answer 方流程 ======================================
async function startAsAnswer() {
    disableButtons();
    log('info', '========== 作为 Answer 方开始 ==========');

    try {
        // createPeerConnection();
        // await getLocalMedia(); 
        const canvas = document.getElementById('videoCanvas');
        rtcPlayer = new WebRtcPlayer(canvas);
        rtcPlayer.setRollType(true);
        rtcPlayer.onLocalSdp((localSdp) => {
            log('info', '创建answer');
            sendSignal('answer', { sdp: localSdp });
            log('success', 'answer已发送');
        });
        rtcPlayer.onIceCandidate((candidate, mid, lineIndex) => {
            sendSignal('candidate', {
                candidate: candidate,
                sdpMid: mid,
                sdpMLineIndex: lineIndex
            });
        })
        rtcPlayer.open();
        if (!recvOnlyMode) {
            await getLocalMedia();
        } else {
            log('info', 'recvonly 模式：不采集本地媒体');
        }

        await connectSSE();

        log('info', '等待对方 Offer...');

    } catch (error) {
        log('error', `Answer 流程失败: ${error.message}`);
        enableButtons();
    }
}

// ====================================== 重置 ======================================
function resetAll() {

    log('info', '开始重置');
    if(rtcPlayer){
        rtcPlayer.destroy();
        rtcPlayer = null;
    }

    if (sseConnection) {
        sseConnection.onopen = null;
        sseConnection.onmessage = null;
        sseConnection.onerror = null;
        sseConnection.close();
        sseConnection = null;
    }

    if (peerConnection) {
        peerConnection.onicecandidate = null;
        peerConnection.ontrack = null;
        peerConnection.onconnectionstatechange = null;
        peerConnection.close();
        peerConnection = null;
    }

    if (localStream) {
        localStream.getTracks().forEach(t => t.stop());
        localStream = null;
    }

    // 清状态
    localCandidateCount = 0;
    remoteCandidateCount = 0;
    localCandidatesList = [];
    remoteCandidatesList = [];

    document.getElementById('localVideo').srcObject = null;
    document.getElementById('remoteVideo').srcObject = null;

    updateStatus('sseStatus', 'disconnected', '未连接');
    //updateStatus('iceStatus', 'disconnected', '未开始');
    updateStatus('connStatus', 'disconnected', '未连接');
    updateCandidateCount();


    // 清日志
    clearLog();

    // UI
    enableButtons();

    log('info', '已重置');
}

// ====================================== 初始化 ======================================
window.onload = () => {
    log('info', '页面就绪，请填写 Token 和 Self ID');
    recvOnlyMode = true
};

window.onbeforeunload = () => {
    if (sseConnection) sseConnection.close();
    if (peerConnection) peerConnection.close();
};


// ==================================== 视频信息 ======================================
async function logOutboundVideoStats() {
    if (!peerConnection) return;

    const stats = await peerConnection.getStats();
    stats.forEach(report => {
        if (report.type === 'outbound-rtp' && report.kind === 'video') {
            console.log('==== 发送端视频编码信息 ====');
            console.log('编码格式: ', report.codecId); // 具体 codec 可以从 stats 查 codec 类型
            console.log('帧率 (fps): ', report.framesPerSecond);
            console.log('分辨率: ', report.frameWidth, 'x', report.frameHeight);
            console.log('发送码率 (bps): ', report.bytesSent * 8 / (report.timestamp / 1000));
        }

        if (report.type === 'codec') {
            console.log('Codec 名称: ', report.mimeType); // "video/VP8", "video/H264" 等
        }
    });
}

function enableRecvOnlyMode() {
    recvOnlyMode = true;
    log('info', '已切换为【仅接收 recvonly 模式】');
}

