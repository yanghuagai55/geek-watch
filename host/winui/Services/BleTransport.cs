using System.Globalization;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace At32Debugger.Services;

/// <summary>BLE 透传：FFE0 服务 / FFE1 特征值，WinRT 直连地址。</summary>
public sealed class BleTransport : IWatchTransport
{
    private static readonly Guid ServiceUuid = new("0000ffe0-0000-1000-8000-00805f9b34fb");
    private static readonly Guid CharacteristicUuid = new("0000ffe1-0000-1000-8000-00805f9b34fb");

    private readonly string _addressText;
    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _characteristic;
    private GattSession? _gattSession;
    private bool _connected;

    public BleTransport(string address)
    {
        _addressText = address;
    }

    public string DisplayName => "蓝牙 BLE";

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
        ulong address = ParseAddress(_addressText);

        BluetoothLEDevice? device = null;
        try
        {
            device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
        }
        catch
        {
        }

        if (device is null)
        {
            await WaitForAdvertisementAsync(address, cancellationToken);
            device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
        }

        if (device is null)
        {
            throw new InvalidOperationException($"找不到 BLE 设备 {_addressText}（确认模块上电、没有被手机占用）");
        }

        try
        {
            var services = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                throw new InvalidOperationException("找不到 FFE0 服务，这个设备可能不是 HC-04");
            }

            var service = services.Services[0];
            var characteristics = await service.GetCharacteristicsForUuidAsync(
                CharacteristicUuid, BluetoothCacheMode.Uncached);
            if (characteristics.Status != GattCommunicationStatus.Success || characteristics.Characteristics.Count == 0)
            {
                service.Dispose();
                throw new InvalidOperationException("找不到 FFE1 特征值");
            }

            var characteristic = characteristics.Characteristics[0];
            characteristic.ValueChanged += OnValueChanged;
            device.ConnectionStatusChanged += OnConnectionStatusChanged;

            var status = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            if (status != GattCommunicationStatus.Success)
            {
                characteristic.ValueChanged -= OnValueChanged;
                device.ConnectionStatusChanged -= OnConnectionStatusChanged;
                service.Dispose();
                throw new InvalidOperationException(
                    $"开启通知失败（{status}），可在设置里解除 BLE 配对后重试");
            }

            try
            {
                _gattSession = await GattSession.FromDeviceIdAsync(device.BluetoothDeviceId);
                _gattSession.MaintainConnection = true;
            }
            catch
            {
                _gattSession = null;
            }

            _device = device;
            _service = service;
            _characteristic = characteristic;
            _connected = true;
            ConnectionChanged?.Invoke(this, true);
        }
        catch
        {
            device.Dispose();
            throw;
        }
    }

    private static async Task WaitForAdvertisementAsync(ulong address, CancellationToken cancellationToken)
    {
        var found = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active,
        };
        watcher.Received += (_, args) =>
        {
            if (args.BluetoothAddress == address)
            {
                found.TrySetResult(true);
            }
        };

        watcher.Start();
        try
        {
            using var registration = cancellationToken.Register(() => found.TrySetCanceled(cancellationToken));
            await Task.WhenAny(found.Task, Task.Delay(6000, cancellationToken));
        }
        finally
        {
            watcher.Stop();
        }
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        try
        {
            var buffer = args.CharacteristicValue;
            using var reader = DataReader.FromBuffer(buffer);
            var data = new byte[buffer.Length];
            reader.ReadBytes(data);
            DataReceived?.Invoke(this, data);
        }
        catch
        {
        }
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        if (sender.ConnectionStatus == BluetoothConnectionStatus.Disconnected && _connected)
        {
            _connected = false;
            ConnectionChanged?.Invoke(this, false);
        }
    }

    public async Task SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        var characteristic = _characteristic ?? throw new InvalidOperationException("BLE 未连接");

        using var writer = new DataWriter();
        writer.WriteBytes(data.ToArray());
        var buffer = writer.DetachBuffer();

        var result = await characteristic.WriteValueWithResultAsync(buffer, GattWriteOption.WriteWithoutResponse);
        if (result.Status != GattCommunicationStatus.Success)
        {
            result = await characteristic.WriteValueWithResultAsync(buffer, GattWriteOption.WriteWithResponse);
        }
        if (result.Status != GattCommunicationStatus.Success)
        {
            throw new IOException($"BLE 写入失败：{result.Status}");
        }
    }

    public async Task DisconnectAsync()
    {
        _connected = false;

        var characteristic = _characteristic;
        _characteristic = null;
        if (characteristic is not null)
        {
            characteristic.ValueChanged -= OnValueChanged;
            try
            {
                await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.None);
            }
            catch
            {
            }
        }

        _gattSession?.Dispose();
        _gattSession = null;
        _service?.Dispose();
        _service = null;

        if (_device is not null)
        {
            _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
            _device.Dispose();
            _device = null;
        }

        ConnectionChanged?.Invoke(this, false);
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
    }
}
