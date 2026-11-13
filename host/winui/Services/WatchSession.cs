using System.Diagnostics;
using System.Text;
using Windows.Devices.Bluetooth;

namespace At32Debugger.Services;

/// <summary>手表每秒回传的遥测数据。</summary>
public sealed record WatchStats(
    uint PrepareUs,
    uint RasterUs,
    uint WaitUs,
    uint FrameUs,
    uint Pixels,
    ushort Bands,
    ushort Dropped,
    uint RamUsed,
    uint RamTotal,
    ushort CpuTenths,
    byte Fps);

/// <summary>
/// 会话层：组帧、收发日志、等待 ok、抓屏重组，以及手表回传的控制/性能帧解析。
/// 传输层由 BLE / SPP / 串口 / 演示四种实现提供。
/// </summary>
public sealed class WatchSession : IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly List<byte> _rx = new();
    private readonly List<byte> _events = new();
    private IWatchTransport? _transport;
    private CancellationTokenSource? _telemetryCts;
    private readonly SemaphoreSlim _snapshotGate = new(1, 1);
    private int _statsFrames;
    private bool _quiet;

    public bool IsConnected => _transport?.IsConnected == true;

    public string ConnectedName => _transport?.DisplayName ?? "—";

    /// <summary>level: info / tx / rx / ok / warn / err</summary>
    public event Action<string, string>? Log;

    public event EventHandler<bool>? ConnectionChanged;

    public event Action<WatchStats>? StatsReceived;

    public async Task<bool> ConnectAsync(TransportKind kind, string endpoint, int baud)
    {
        await DisconnectAsync();

        IWatchTransport transport;
        try
        {
            transport = CreateTransport(kind, endpoint, baud);
        }
        catch (Exception ex)
        {
            Log?.Invoke("err", $"连接失败：{ex.Message}");
            ConnectionChanged?.Invoke(this, false);
            return false;
        }

        transport.DataReceived += OnDataReceived;
        transport.ConnectionChanged += OnTransportConnectionChanged;

        try
        {
            await transport.ConnectAsync();
        }
        catch (Exception ex)
        {
            transport.DataReceived -= OnDataReceived;
            transport.ConnectionChanged -= OnTransportConnectionChanged;
            await SafeDisposeAsync(transport);
            Log?.Invoke("err", $"连接失败：{ex.Message}");
            if (kind == TransportKind.Ble)
            {
                Log?.Invoke("warn", "若提示开启通知失败，可在设置里解除 BLE 配对后重连");
            }
            ConnectionChanged?.Invoke(this, false);
            return false;
        }

        _transport = transport;
        lock (_gate)
        {
            _rx.Clear();
            _events.Clear();
        }

        Log?.Invoke("info", $"已连接 {transport.DisplayName}（{endpoint}）");
        ConnectionChanged?.Invoke(this, true);
        await SendFrameAsync(Protocol.CmdNop, Array.Empty<byte>(), waitAck: true);

        bool timeOk = await SyncTimeAsync();
        Log?.Invoke(timeOk ? "info" : "warn", timeOk ? "已自动校准时间" : "自动校时无回应");
        await GoHomeAsync();
        StartTelemetry();
        return true;
    }

    public async Task DisconnectAsync()
    {
        var transport = _transport;
        if (transport is null)
        {
            return;
        }

        _transport = null;
        StopTelemetry();
        transport.DataReceived -= OnDataReceived;
        transport.ConnectionChanged -= OnTransportConnectionChanged;

        try
        {
            await transport.DisconnectAsync();
        }
        catch (Exception ex)
        {
            Log?.Invoke("warn", $"断开出错：{ex.Message}");
        }
        await SafeDisposeAsync(transport);

        lock (_gate)
        {
            _rx.Clear();
            _events.Clear();
        }
        Log?.Invoke("info", "已断开");
        ConnectionChanged?.Invoke(this, false);
    }

    public async Task<bool> SendFrameAsync(byte cmd, byte[]? payload = null, bool waitAck = true)
    {
        var transport = _transport;
        if (transport is null || !transport.IsConnected)
        {
            Log?.Invoke("warn", "未连接");
            return false;
        }

        var frame = Protocol.Frame(cmd, payload ?? Array.Empty<byte>());
        lock (_gate)
        {
            _rx.Clear();
        }

        try
        {
            await transport.SendAsync(frame);
        }
        catch (Exception ex)
        {
            Log?.Invoke("err", $"写入失败：{ex.Message}");
            return false;
        }

        Log?.Invoke("tx", "TX " + Protocol.Hex(frame));
        if (!waitAck)
        {
            return true;
        }

        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < 2000)
        {
            lock (_gate)
            {
                if (ContainsOk())
                {
                    Log?.Invoke("ok", $"RX ok  ({sw.ElapsedMilliseconds} ms)");
                    return true;
                }
            }
            await Task.Delay(20);
        }

        Log?.Invoke("warn", "无回应");
        return false;
    }

    /// <summary>自定义发送：不等待 ok，用于测试任意字节。</summary>
    public async Task SendRawAsync(byte[] data)
    {
        var transport = _transport;
        if (transport is null || !transport.IsConnected)
        {
            Log?.Invoke("warn", "未连接");
            return;
        }

        try
        {
            await transport.SendAsync(data);
            Log?.Invoke("tx", "TX " + Protocol.Hex(data));
        }
        catch (Exception ex)
        {
            Log?.Invoke("err", $"写入失败：{ex.Message}");
        }
    }

    public Task<bool> SyncTimeAsync()
    {
        var now = DateTime.Now;
        var payload = new byte[]
        {
            (byte)(now.Year & 0xFF), (byte)(now.Year >> 8),
            (byte)now.Month, (byte)now.Day,
            (byte)now.Hour, (byte)now.Minute, (byte)now.Second,
        };
        return SendFrameAsync(Protocol.CmdTime, payload);
    }

    public Task<bool> SetPlayStateAsync(bool playing) =>
        SendFrameAsync(Protocol.CmdState, new byte[] { (byte)(playing ? 1 : 0) });

    /// <summary>请求性能统计，结果通过手表回传的 CMD_STATS 帧异步打印到日志。</summary>
    public Task<bool> RequestPerfAsync()
    {
        if (_quiet)
        {
            return Task.FromResult(false);
        }
        return SendFrameAsync(Protocol.CmdPerf, Array.Empty<byte>(), waitAck: false);
    }

    /// <summary>关闭手表子页/菜单，回到表盘主页。</summary>
    public Task<bool> GoHomeAsync() =>
        SendFrameAsync(Protocol.CmdHome, Array.Empty<byte>());

    /// <summary>按键是边沿触发，且固件只在调试模式下响应蓝牙按键。</summary>
    public async Task TapKeyAsync(byte mask, int holdMs = 90)
    {
        await SendFrameAsync(Protocol.CmdDebug, new byte[] { 1 }, waitAck: true);
        await SendFrameAsync(Protocol.CmdKey, new[] { (byte)(mask & 7) }, waitAck: false);
        await Task.Delay(holdMs);
        await SendFrameAsync(Protocol.CmdKey, new byte[] { 0 }, waitAck: false);
        await Task.Delay(200);
    }

    /// <summary>读传感器调试快照，手表回一行 "DBG ..."。</summary>
    public async Task<string?> QuerySensorAsync()
    {
        var transport = _transport;
        if (transport is null || !transport.IsConnected)
        {
            Log?.Invoke("warn", "未连接");
            return null;
        }

        lock (_gate)
        {
            _rx.Clear();
        }
        await SendFrameAsync(Protocol.CmdSensor, Array.Empty<byte>(), waitAck: false);

        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < 3000)
        {
            string text;
            lock (_gate)
            {
                text = Encoding.UTF8.GetString(_rx.ToArray());
            }

            int index = text.IndexOf("DBG", StringComparison.Ordinal);
            if (index >= 0)
            {
                int end = text.IndexOfAny(new[] { '\r', '\n' }, index);
                if (end > index)
                {
                    string line = text[index..end];
                    Log?.Invoke("info", line);
                    return line;
                }
            }
            await Task.Delay(30);
        }

        Log?.Invoke("warn", "传感器无回应");
        return null;
    }

    /// <summary>解除 Windows 的 BLE 配对，修复 start notify Unreachable。</summary>
    public async Task<bool> UnpairBleAsync(string address)
    {
        try
        {
            ulong parsed = BleTransport.ParseAddress(address);
            var device = await BluetoothLEDevice.FromBluetoothAddressAsync(parsed);
            if (device is null)
            {
                Log?.Invoke("warn", "系统里没有这台设备，无法解绑");
                return false;
            }

            var info = device.DeviceInformation;
            if (info.Pairing.IsPaired)
            {
                var result = await info.Pairing.UnpairAsync();
                Log?.Invoke("ok", $"解除配对：{result.Status}");
            }
            else
            {
                Log?.Invoke("info", "没有配对记录，跳过解绑");
            }

            device.Dispose();
            return true;
        }
        catch (Exception ex)
        {
            Log?.Invoke("err", $"解绑失败：{ex.Message}");
            return false;
        }
    }

    public async Task<SnapshotImage?> RequestSnapshotAsync(IProgress<int>? progress = null,
        CancellationToken cancellationToken = default)
    {
        await _snapshotGate.WaitAsync(cancellationToken);
        try
        {
            return await RequestSnapshotCoreAsync(progress, cancellationToken);
        }
        finally
        {
            _snapshotGate.Release();
        }
    }

    private async Task<SnapshotImage?> RequestSnapshotCoreAsync(IProgress<int>? progress = null,
        CancellationToken cancellationToken = default)
    {
        if (_transport is null || !_transport.IsConnected)
        {
            Log?.Invoke("warn", "未连接");
            return null;
        }

        StopTelemetry();
        await Task.Delay(50, cancellationToken);
        _quiet = true;
        try
        {
            lock (_gate)
            {
                _rx.Clear();
            }
            await SendFrameAsync(Protocol.CmdSnapshot, Array.Empty<byte>(), waitAck: false);

            var sw = Stopwatch.StartNew();
            int start = -1;
            int total = 0;
            while (sw.ElapsedMilliseconds < 180_000)
            {
                cancellationToken.ThrowIfCancellationRequested();
                byte[] buffer;
                lock (_gate)
                {
                    buffer = _rx.ToArray();
                }

                if (start < 0)
                {
                    int i = IndexOfHeader(buffer);
                    if (i >= 0 && buffer.Length >= i + 6)
                    {
                        start = i;
                        total = buffer[i + 3] | (buffer[i + 4] << 8) | (buffer[i + 5] << 16);
                        Log?.Invoke("info", $"帧头 {total} 字节");
                    }
                }
                else
                {
                    int got = buffer.Length - start - 6;
                    progress?.Report(Math.Clamp(got * 100 / Math.Max(1, total), 0, 100));
                    if (buffer.Length >= start + 6 + total + 1)
                    {
                        var payload = new byte[total];
                        Array.Copy(buffer, start + 6, payload, 0, total);
                        var image = SnapshotDecoder.Decode(payload);
                        if (image is null)
                        {
                            Log?.Invoke("err", "像素长度不对");
                            return null;
                        }
                        Log?.Invoke("ok", $"抓屏完成 {image.Width}×{image.Height}，{sw.Elapsed.TotalSeconds:F1} 秒");
                        return image;
                    }
                }

                await Task.Delay(25, cancellationToken);
            }

            Log?.Invoke("err", "抓屏失败：超时或数据不全");
            return null;
        }
        finally
        {
            _quiet = false;
            progress?.Report(0);
            if (_transport?.IsConnected == true)
            {
                StartTelemetry();
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
    }

    private static IWatchTransport CreateTransport(TransportKind kind, string endpoint, int baud) => kind switch
    {
        TransportKind.Ble => new BleTransport(endpoint),
        TransportKind.Spp => new SppTransport(endpoint, 1),
        TransportKind.Serial => new SerialTransport(endpoint, baud),
        _ => new DemoTransport(),
    };

    private static async Task SafeDisposeAsync(IWatchTransport transport)
    {
        try
        {
            await transport.DisposeAsync();
        }
        catch
        {
        }
    }

    private void OnTransportConnectionChanged(object? sender, bool connected)
    {
        if (connected || sender is not IWatchTransport transport || !ReferenceEquals(_transport, transport))
        {
            return;
        }

        _transport = null;
        StopTelemetry();
        transport.DataReceived -= OnDataReceived;
        transport.ConnectionChanged -= OnTransportConnectionChanged;
        lock (_gate)
        {
            _rx.Clear();
            _events.Clear();
        }
        Log?.Invoke("warn", "链路已断开");
        ConnectionChanged?.Invoke(this, false);
    }

    private void OnDataReceived(object? sender, byte[] data)
    {
        bool isTelemetry = data.Length >= 2 && data[0] == Protocol.Mark &&
                           (data[1] == Protocol.CmdStats || data[1] == Protocol.CmdCtrl);

        lock (_gate)
        {
            _rx.AddRange(data);
            if (!_quiet && _rx.Count > 8192)
            {
                _rx.RemoveRange(0, _rx.Count - 4096);
            }
            if (!_quiet)
            {
                _events.AddRange(data);
            }
        }

        if (!_quiet)
        {
            if (!isTelemetry)
            {
                Log?.Invoke("rx", "RX " + Protocol.Hex(data));
            }
            ProcessEvents();
        }
    }

    private void ProcessEvents()
    {
        while (true)
        {
            (byte Cmd, byte[] Payload)? frame;
            lock (_gate)
            {
                frame = TakeEventFrame();
            }
            if (frame is null)
            {
                break;
            }
            HandleEvent(frame.Value.Cmd, frame.Value.Payload);
        }
    }

    /// <summary>从回传字节流里取出完整帧；调用方需持有 _gate。</summary>
    private (byte Cmd, byte[] Payload)? TakeEventFrame()
    {
        for (int i = 0; i + 4 <= _events.Count; i++)
        {
            if (_events[i] != Protocol.Mark)
            {
                continue;
            }

            byte cmd = _events[i + 1];
            int len = _events[i + 2];
            if (cmd == Protocol.CmdImage && len == 0xFF)
            {
                // 图像流只在抓屏流程里解析，避免把像素误当帧
                _events.Clear();
                return null;
            }

            int total = 4 + len;
            if (i + total > _events.Count)
            {
                return null;
            }

            var payload = new byte[len];
            for (int j = 0; j < len; j++)
            {
                payload[j] = _events[i + 3 + j];
            }
            _events.RemoveRange(0, i + total);
            return (cmd, payload);
        }

        if (_events.Count > 4096)
        {
            _events.RemoveRange(0, _events.Count - 4096);
        }
        return null;
    }

    private void HandleEvent(byte cmd, byte[] payload)
    {
        if (cmd == Protocol.CmdCtrl && payload.Length >= 1)
        {
            Log?.Invoke("info", $"手表控制：{CtrlName(payload[0])}");
            return;
        }

        if (cmd == Protocol.CmdStats && payload.Length >= 32)
        {
            uint prepare = BitConverter.ToUInt32(payload, 0);
            uint raster = BitConverter.ToUInt32(payload, 4);
            uint wait = BitConverter.ToUInt32(payload, 8);
            uint frame = BitConverter.ToUInt32(payload, 12);
            uint pixels = BitConverter.ToUInt32(payload, 16);
            uint bandsPacked = BitConverter.ToUInt32(payload, 20);
            uint ramPacked = BitConverter.ToUInt32(payload, 24);
            uint ramFpsPacked = BitConverter.ToUInt32(payload, 28);

            ushort bands = (ushort)(bandsPacked & 0xFFFF);
            ushort dropped = (ushort)(bandsPacked >> 16);
            uint ramUsed = ramPacked & 0xFFFF;
            ushort cpuTenths = (ushort)(ramPacked >> 16);
            uint ramTotal = ramFpsPacked & 0xFFFF;
            byte fps = (byte)(ramFpsPacked >> 16);

            var stats = new WatchStats(prepare, raster, wait, frame, pixels, bands, dropped,
                ramUsed, ramTotal, cpuTenths, fps);
            StatsReceived?.Invoke(stats);

            _statsFrames++;
            if (_statsFrames == 1 || _statsFrames % 10 == 0)
            {
                string ramText = ramTotal > 0
                    ? $"RAM {ramUsed / 1024.0:F1}/{ramTotal / 1024.0:F1} KB"
                    : "RAM —";
                Log?.Invoke("ok", $"遥测 {fps} fps · {ramText} · CPU {cpuTenths / 10.0:F1}%");
            }
        }
    }

    private static string CtrlName(byte action) => action switch
    {
        1 => "播放",
        2 => "暂停",
        3 => "下一首",
        4 => "上一首",
        _ => action.ToString(),
    };

    private void StartTelemetry()
    {
        StopTelemetry();
        _telemetryCts = new CancellationTokenSource();
        _ = TelemetryLoopAsync(_telemetryCts.Token);
    }

    private void StopTelemetry()
    {
        var cts = _telemetryCts;
        _telemetryCts = null;
        cts?.Cancel();
        cts?.Dispose();
    }

    private async Task TelemetryLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            if (_transport?.IsConnected != true)
            {
                break;
            }

            await SendFrameSilentAsync(Protocol.CmdPerf);
            try
            {
                await Task.Delay(1000, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task SendFrameSilentAsync(byte cmd)
    {
        var transport = _transport;
        if (transport is null || !transport.IsConnected || _quiet)
        {
            return;
        }

        try
        {
            await transport.SendAsync(Protocol.Frame(cmd));
        }
        catch
        {
        }
    }

    private bool ContainsOk()
    {
        for (int i = 0; i + 1 < _rx.Count; i++)
        {
            if (_rx[i] == (byte)'o' && _rx[i + 1] == (byte)'k')
            {
                return true;
            }
        }
        return false;
    }

    private static int IndexOfHeader(byte[] data)
    {
        for (int i = 0; i + 2 < data.Length; i++)
        {
            if (data[i] == Protocol.Mark && data[i + 1] == Protocol.CmdImage && data[i + 2] == 0xFF)
            {
                return i;
            }
        }
        return -1;
    }
}
