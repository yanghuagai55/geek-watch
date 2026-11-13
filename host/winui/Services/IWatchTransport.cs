namespace At32Debugger.Services;

/// <summary>传输层接口：BLE / SPP / 串口以后各自实现一份，前端只依赖这里。</summary>
public interface IWatchTransport : IAsyncDisposable
{
    string DisplayName { get; }

    bool IsConnected { get; }

    event EventHandler<bool>? ConnectionChanged;

    event EventHandler<byte[]>? DataReceived;

    Task ConnectAsync(CancellationToken cancellationToken = default);

    Task DisconnectAsync();

    Task SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default);
}

public enum TransportKind
{
    Ble,
    Spp,
    Serial,
    Demo,
}
