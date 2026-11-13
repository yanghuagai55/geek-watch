using System.Text;

namespace At32Debugger.Services;

/// <summary>
/// 前端阶段的本地演示通道：不碰硬件，按协议回 ok，并按帧头发一张 128×160 表盘图。
/// 真实 BLE / SPP / 串口实现接入后，界面无需改动。
/// </summary>
public sealed class DemoTransport : IWatchTransport
{
    private bool _connected;
    private CancellationTokenSource? _stream;

    public string DisplayName => "演示";

    public bool IsConnected => _connected;

    public event EventHandler<bool>? ConnectionChanged;

    public event EventHandler<byte[]>? DataReceived;

    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        await Task.Delay(450, cancellationToken);
        _connected = true;
        ConnectionChanged?.Invoke(this, true);
    }

    public Task DisconnectAsync()
    {
        _stream?.Cancel();
        _stream?.Dispose();
        _stream = null;
        _connected = false;
        ConnectionChanged?.Invoke(this, false);
        return Task.CompletedTask;
    }

    public Task SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        if (!_connected)
        {
            throw new InvalidOperationException("演示通道未连接");
        }

        var frame = data.ToArray();
        if (!TryParse(frame, out byte cmd, out bool xorOk) || !xorOk)
        {
            _ = EmitLaterAsync(Encoding.ASCII.GetBytes("bad"), 40);
            return Task.CompletedTask;
        }

        switch (cmd)
        {
            case Protocol.CmdSnapshot:
                _stream?.Cancel();
                _stream?.Dispose();
                _stream = new CancellationTokenSource();
                _ = StreamSnapshotAsync(_stream.Token);
                break;
            case Protocol.CmdSensor:
                _ = EmitLaterAsync(Encoding.ASCII.GetBytes("DBG DEMO  MPU6050 0x68  QMC5883P 0x2C  BAT 3.92V\r\n"), 60);
                break;
            case Protocol.CmdPerf:
                _ = EmitStatsAsync(40);
                break;
            default:
                _ = EmitLaterAsync(Encoding.ASCII.GetBytes("ok"), 40);
                break;
        }
        return Task.CompletedTask;
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
    }

    private async Task EmitLaterAsync(byte[] data, int delayMs)
    {
        await Task.Delay(delayMs);
        if (_connected)
        {
            DataReceived?.Invoke(this, data);
        }
    }

    private async Task EmitStatsAsync(int delayMs)
    {
        await Task.Delay(delayMs);
        if (!_connected)
        {
            return;
        }

        double t = DateTime.Now.TimeOfDay.TotalSeconds;
        uint ramTotal = 16384;
        uint ramUsed = (uint)(8200 + 700 * Math.Sin(t / 2.0));
        ushort cpu = (ushort)(80 + 70 * Math.Abs(Math.Sin(t / 1.5)));
        byte fps = (byte)(59 + DateTime.Now.Second % 2);

        var data = new byte[36];
        data[0] = Protocol.Mark;
        data[1] = Protocol.CmdStats;
        data[2] = 0x20;
        WriteUInt32(data, 3, 360);
        WriteUInt32(data, 7, 480);
        WriteUInt32(data, 11, 8000);
        WriteUInt32(data, 15, 16000);
        WriteUInt32(data, 19, 61440);
        WriteUInt32(data, 23, 4);
        WriteUInt32(data, 27, (ramUsed & 0xFFFF) | ((uint)cpu << 16));
        WriteUInt32(data, 31, (ramTotal & 0xFFFF) | ((uint)fps << 16));

        byte x = 0;
        for (int i = 0; i < data.Length - 1; i++)
        {
            x ^= data[i];
        }
        data[^1] = x;
        DataReceived?.Invoke(this, data);
    }

    private static void WriteUInt32(byte[] data, int offset, uint value)
    {
        data[offset] = (byte)value;
        data[offset + 1] = (byte)(value >> 8);
        data[offset + 2] = (byte)(value >> 16);
        data[offset + 3] = (byte)(value >> 24);
    }

    private async Task StreamSnapshotAsync(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(80, cancellationToken);
            if (!_connected)
            {
                return;
            }
            DataReceived?.Invoke(this, Encoding.ASCII.GetBytes("ok"));

            var payload = BuildImagePayload(128, 160);
            var header = new byte[]
            {
                Protocol.Mark, Protocol.CmdImage, 0xFF,
                (byte)(payload.Length & 0xFF),
                (byte)((payload.Length >> 8) & 0xFF),
                (byte)((payload.Length >> 16) & 0xFF),
            };
            DataReceived?.Invoke(this, header);

            const int chunk = 512;
            for (int offset = 0; offset < payload.Length; offset += chunk)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int n = Math.Min(chunk, payload.Length - offset);
                var part = new byte[n];
                Array.Copy(payload, offset, part, 0, n);
                DataReceived?.Invoke(this, part);
                await Task.Delay(14, cancellationToken);
            }

            DataReceived?.Invoke(this, new byte[] { 0x5A });
        }
        catch (OperationCanceledException)
        {
        }
    }

    private static byte[] BuildImagePayload(int width, int height)
    {
        var bgra = DemoFrame.Build(width, height);
        var payload = new byte[4 + width * height * 2];
        payload[0] = (byte)(width & 0xFF);
        payload[1] = (byte)(width >> 8);
        payload[2] = (byte)(height & 0xFF);
        payload[3] = (byte)(height >> 8);

        for (int i = 0; i < width * height; i++)
        {
            int o = i * 4;
            int b = bgra[o];
            int g = bgra[o + 1];
            int r = bgra[o + 2];
            int v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            payload[4 + i * 2] = (byte)(v >> 8);
            payload[5 + i * 2] = (byte)v;
        }
        return payload;
    }

    private static bool TryParse(byte[] frame, out byte cmd, out bool xorOk)
    {
        cmd = 0;
        xorOk = false;
        if (frame.Length < 4 || frame[0] != Protocol.Mark)
        {
            return false;
        }

        int len = frame[2];
        if (frame.Length != len + 4)
        {
            return false;
        }

        byte x = 0;
        for (int i = 0; i < frame.Length - 1; i++)
        {
            x ^= frame[i];
        }

        cmd = frame[1];
        xorOk = x == frame[^1];
        return true;
    }
}
