using System;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using AvoxCommon;
using AvoxNet;

namespace AvoxControls.RtcPlayer
{
    public class SSEClient : IDisposable
    {
        private HttpClient httpClient;
        private bool isRunning;
        private Task listenTask;
        private string signalServer;
        private string token;
        private string selfId;
        private RtcPlayerController controller;

        public event Action<bool> ConnectionStatusChanged;
        public event Action<string> MessageReceived;
        public event Action<Exception> ErrorOccurred;

        public bool IsConnected { get; private set; }

        public SSEClient(RtcPlayerController controller)
        {
            this.controller = controller;
            httpClient = new HttpClient();
            httpClient.Timeout = TimeSpan.FromSeconds(30);
        }

        public async Task ConnectAsync(string signalServer, string token, string selfId)
        {
            this.signalServer = signalServer.TrimEnd('/');
            this.token = token;
            this.selfId = selfId;

            try
            {
                IsConnected = false;
                ConnectionStatusChanged?.Invoke(false);

                string sseUrl = $"{this.signalServer}?token={Uri.EscapeDataString(token)}&self_id={Uri.EscapeDataString(selfId)}&method=sse&timeout=100000";
                AvoxWrapper.logMsg(LogLevel.info, $"SSE连接: {sseUrl}");

                isRunning = true;
                listenTask = Task.Run(async () => await ListenAsync(sseUrl));
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"SSE连接失败: {ex.Message}");
                ErrorOccurred?.Invoke(ex);
                ConnectionStatusChanged?.Invoke(false);
            }
        }

        private async Task ListenAsync(string url)
        {
            try
            {
                using (var request = new HttpRequestMessage(HttpMethod.Get, url))
                {
                    request.Headers.Accept.Add(new System.Net.Http.Headers.MediaTypeWithQualityHeaderValue("text/event-stream"));
                    request.Headers.Add("Cache-Control", "no-cache");
                    request.Headers.Add("Connection", "keep-alive");

                    using (var response = await httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead))
                    {
                        response.EnsureSuccessStatusCode();

                        IsConnected = true;
                        ConnectionStatusChanged?.Invoke(true);
                        AvoxWrapper.logMsg(LogLevel.info, "SSE连接成功");

                        using (var stream = await response.Content.ReadAsStreamAsync())
                        using (var reader = new StreamReader(stream, Encoding.UTF8))
                        {
                            string line;
                            StringBuilder eventData = new StringBuilder();

                            while (isRunning && (line = await reader.ReadLineAsync()) != null)
                            {
                                if (line.StartsWith("data:"))
                                {
                                    string data = line.Substring(5).TrimStart();
                                    eventData.AppendLine(data);
                                }
                                else if (line == string.Empty)
                                {
                                    string eventDataString = eventData.ToString().Trim();
                                    if (!string.IsNullOrEmpty(eventDataString))
                                    {
                                        MessageReceived?.Invoke(eventDataString);
                                        HandleSSEMessage(eventDataString);
                                    }
                                    eventData.Clear();
                                }
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"SSE监听失败: {ex.Message}");
                ErrorOccurred?.Invoke(ex);
            }
            finally
            {
                IsConnected = false;
                ConnectionStatusChanged?.Invoke(false);
                AvoxWrapper.logMsg(LogLevel.info, "SSE连接已关闭");
            }
        }

        private void HandleSSEMessage(string data)
        {
            try
            {
                var message = System.Text.Json.JsonSerializer.Deserialize<System.Text.Json.JsonElement>(data);

                if (message.TryGetProperty("code", out var codeElement) &&
                    message.TryGetProperty("msg", out var msgElement) &&
                    codeElement.GetInt32() == 200 &&
                    msgElement.GetString() == "connected")
                {
                    AvoxWrapper.logMsg(LogLevel.info, "SSE连接确认");
                    return;
                }

                string method = "";
                if (message.TryGetProperty("method", out var methodElement))
                {
                    method = methodElement.GetString();
                }
                else if (message.TryGetProperty("type", out var typeElement))
                {
                    method = typeElement.GetString();
                }

                var messageData = message.TryGetProperty("data", out var dataElement) ? dataElement : message;

                if (method == "offer")
                {
                    AvoxWrapper.logMsg(LogLevel.info, "收到Offer, 设置对端描述...");
                    if (messageData.TryGetProperty("sdp", out var sdpElement))
                    {
                        string sdp = sdpElement.GetString();
                        if (controller != null)
                        {
                            controller.RemoteSdp = sdp;
                        }
                        AvoxWrapper.logMsg(LogLevel.info, "对端描述设置成功");
                    }
                }
                else if (method == "answer")
                {
                    AvoxWrapper.logMsg(LogLevel.info, "收到Answer, 设置对端描述...");
                    if (messageData.TryGetProperty("sdp", out var sdpElement))
                    {
                        string sdp = sdpElement.GetString();
                        if (controller != null)
                        {
                            controller.RemoteSdp = sdp;
                        }
                        AvoxWrapper.logMsg(LogLevel.info, "对端描述设置成功");
                    }
                }
                else if (method == "candidate")
                {
                    string candidate = "";
                    string sdpMid = "0";
                    int sdpMLineIndex = 0;

                    if (messageData.TryGetProperty("candidate", out var candidateElement))
                    {
                        candidate = candidateElement.GetString();
                    }

                    if (messageData.TryGetProperty("sdpMid", out var midElement))
                    {
                        sdpMid = midElement.GetString();
                    }

                    if (messageData.TryGetProperty("sdpMLineIndex", out var indexElement))
                    {
                        sdpMLineIndex = indexElement.GetInt32();
                    }

                    if (!string.IsNullOrEmpty(candidate) && controller != null)
                    {
                        AvoxWrapper.logMsg(LogLevel.info, $"收到候选者: sdpMid={sdpMid}, sdpMLineIndex={sdpMLineIndex}");
                        controller.AddIceCandidate(candidate, sdpMid, sdpMLineIndex);
                    }
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"处理SSE消息失败: {ex.Message}");
                ErrorOccurred?.Invoke(ex);
            }
        }

        public async Task<System.Text.Json.JsonElement> SendSignalAsync(string method, object data)
        {
            try
            {
                string url = $"{signalServer}?token={Uri.EscapeDataString(token)}&self_id={Uri.EscapeDataString(selfId)}&method={method}&timeout=30000";

                var body = new
                {
                    @event = "webrtc.live",
                    method = method,
                    stream_id = "stream000000000000",
                    version = 1,
                    data = data
                };

                var content = new StringContent(
                    System.Text.Json.JsonSerializer.Serialize(body),
                    Encoding.UTF8,
                    "application/json"
                );

                using (var response = await httpClient.PostAsync(url, content))
                {
                    response.EnsureSuccessStatusCode();
                    string responseText = await response.Content.ReadAsStringAsync();

                    if (string.IsNullOrEmpty(responseText))
                    {
                        return default;
                    }

                    return System.Text.Json.JsonSerializer.Deserialize<System.Text.Json.JsonElement>(responseText);
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"发送信令失败: {ex.Message}");
                ErrorOccurred?.Invoke(ex);
                throw;
            }
        }

        public void Close()
        {
            isRunning = false;
            if (listenTask != null && !listenTask.IsCompleted)
            {
                listenTask.Wait(1000);
            }
        }

        public void Dispose()
        {
            Close();
            httpClient?.Dispose();
        }
    }
}