using System.IO.Ports;

namespace At32Debugger.Services;

/// <summary>CH340 / USB-TTL 串口链路。</summary>
public sealed class SerialTransport : IWatchTransport
{
    private readonly string _portName;
    private readonly int _baud;
    private SerialPort? _port;
    private CancellationTokenSource? _cts;
    private Task? _reader;
    private bool _connected;

    public SerialTransport(string portName, int baud)
    {
        _portName = portName;
        _baud = baud;
    }

    public string DisplayName => "串口";

    public bool IsConnected => _connected;

    public event EventHandler<bool>? ConnectionChanged;

    public event EventHandler<byte[]>? DataReceived;

    public Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        var port = new SerialPort(_portName, _baud)
        {
            ReadTimeout = 50,
            WriteTimeout = 1000,
            DtrEnable = false,
            RtsEnable = false,
        };
        port.Open();

        _port = port;
        _connected = true;
        _cts = new CancellationTokenSource();
        _reader = Task.Run(() => ReadLoop(port, _cts.Token));
        ConnectionChanged?.Invoke(this, true);
        return Task.CompletedTask;
    }

    private void ReadLoop(SerialPort port, CancellationToken cancellationToken)
    {
        var buffer = new byte[512];
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                int n = port.Read(buffer, 0, buffer.Length);
                if (n > 0)
                {
                    var data = new byte[n];
                    Array.Copy(buffer, data, n);
                    DataReceived?.Invoke(this, data);
                }
            }
            catch (TimeoutException)
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
        var port = _port ?? throw new InvalidOperationException("串口未打开");
        port.Write(data.ToArray(), 0, data.Length);
        return Task.CompletedTask;
    }

    public Task DisconnectAsync()
    {
        var cts = _cts;
        _cts = null;
        cts?.Cancel();

        var port = _port;
        _port = null;
        _connected = false;
        try
        {
            port?.Close();
            port?.Dispose();
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
