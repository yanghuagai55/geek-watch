using System.Globalization;
using System.Net;
using System.Net.Sockets;

namespace At32Debugger.Services;

/// <summary>
/// SOCKADDR_BTH（ws2bth.h）按 1 字节对齐：
/// family@0（2 字节）、btAddr@2（8 字节）、serviceClassId@10（16 字节全 0）、port@26（4 字节），共 30 字节。
/// </summary>
public sealed class BluetoothRfcommEndPoint : EndPoint
{
    public const int AddressFamilyBth = 32;
    private const int SockaddrSize = 30;

    private readonly ulong _address;
    private readonly int _channel;

    public BluetoothRfcommEndPoint(ulong address, int channel)
    {
        _address = address;
        _channel = channel;
    }

    public override AddressFamily AddressFamily => (AddressFamily)AddressFamilyBth;

    public override SocketAddress Serialize()
    {
        var bytes = new byte[SockaddrSize];
        BitConverter.GetBytes((ushort)AddressFamilyBth).CopyTo(bytes, 0);
        BitConverter.GetBytes(_address).CopyTo(bytes, 2);
        BitConverter.GetBytes((uint)_channel).CopyTo(bytes, 26);

        var socketAddress = new SocketAddress(AddressFamily, bytes.Length);
        for (int i = 0; i < bytes.Length; i++)
        {
            socketAddress[i] = bytes[i];
        }
        return socketAddress;
    }

    public override EndPoint Create(SocketAddress socketAddress) => this;
}

/// <summary>经典蓝牙 SPP：裸 RFCOMM socket，不经过系统 COM 口。</summary>
public sealed class SppTransport : IWatchTransport
{
    private readonly ulong _address;
    private readonly string _displayAddress;
    private readonly int _channel;
    private Socket? _socket;
    private CancellationTokenSource? _cts;
    private Task? _reader;
    private bool _connected;

    public SppTransport(string address, int channel = 1)
    {
        _displayAddress = address;
        _address = ParseAddress(address);
        _channel = channel;
    }

    public string DisplayName => "经典 SPP";

    public bool IsConnected => _connected;

    public event EventHandler<bool>? ConnectionChanged;

    public event EventHandler<byte[]>? DataReceived;

    public static ulong ParseAddress(string text)
    {
        string cleaned = text.Replace(":", string.Empty).Replace("-", string.Empty).Trim();
        return ulong.Parse(cleaned, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
    }

    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        Exception? last = null;
        for (int attempt = 0; attempt < 3; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Socket? socket = null;
            try
            {
                socket = new Socket((AddressFamily)BluetoothRfcommEndPoint.AddressFamilyBth,
                    SocketType.Stream, (ProtocolType)3)
                {
                    ReceiveTimeout = 20,
                    SendTimeout = 3000,
                };
                await Task.Run(() => socket.Connect(new BluetoothRfcommEndPoint(_address, _channel)),
                    cancellationToken);
                _socket = socket;
                _connected = true;
                _cts = new CancellationTokenSource();
                _reader = Task.Run(() => ReadLoop(socket, _cts.Token));
                ConnectionChanged?.Invoke(this, true);
                return;
            }
            catch (Exception ex)
            {
                last = ex;
                socket?.Dispose();
                await Task.Delay(400, cancellationToken);
            }
        }

        throw new IOException($"SPP 连接 {_displayAddress} 通道 {_channel} 失败：{last?.Message}", last);
    }

    private void ReadLoop(Socket socket, CancellationToken cancellationToken)
    {
        var buffer = new byte[1024];
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                int n = socket.Receive(buffer);
                if (n <= 0)
                {
                    break;
                }

                var data = new byte[n];
                Array.Copy(buffer, data, n);
                DataReceived?.Invoke(this, data);
            }
            catch (SocketException ex) when (ex.SocketErrorCode is SocketError.TimedOut or SocketError.WouldBlock)
            {
            }
            catch
            {
                break;
            }
        }

        if (!cancellationToken.IsCancellationRequested)
        {
            _connected = false;
            ConnectionChanged?.Invoke(this, false);
        }
    }

    public Task SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        var socket = _socket ?? throw new InvalidOperationException("SPP 未连接");
        byte[] bytes = data.ToArray();
        int offset = 0;
        while (offset < bytes.Length)
        {
            int n = socket.Send(bytes, offset, bytes.Length - offset, SocketFlags.None);
            if (n <= 0)
            {
                throw new IOException("SPP 发送失败");
            }
            offset += n;
        }
        return Task.CompletedTask;
    }

    public Task DisconnectAsync()
    {
        var cts = _cts;
        _cts = null;
        cts?.Cancel();

        var socket = _socket;
        _socket = null;
        _connected = false;
        try
        {
            socket?.Shutdown(SocketShutdown.Both);
        }
        catch
        {
        }
        try
        {
            socket?.Close();
            socket?.Dispose();
        }
        catch
        {
        }

        cts?.Dispose();
        ConnectionChanged?.Invoke(this, false);
        return Task.CompletedTask;
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        if (_reader is not null)
        {
            try
            {
                await _reader;
            }
            catch
            {
            }
        }
    }
}
