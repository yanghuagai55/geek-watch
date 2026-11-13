using System.Collections.ObjectModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using At32Debugger.Services;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Pickers;
using Windows.UI;

namespace At32Debugger;

public sealed partial class MainWindow : Window
{
    private const string BleAddress = "C4:26:01:08:02:0C";
    private const string SppAddress = "04:26:01:08:02:0C";
    private const int PreferredWidth = 1180;
    private const int PreferredHeight = 760;

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    private static readonly SolidColorBrush InfoBrush = new(Color.FromArgb(255, 154, 160, 166));
    private static readonly SolidColorBrush TxBrush = new(Color.FromArgb(255, 232, 162, 74));
    private static readonly SolidColorBrush RxBrush = new(Color.FromArgb(255, 108, 170, 224));
    private static readonly SolidColorBrush OkBrush = new(Color.FromArgb(255, 92, 184, 92));
    private static readonly SolidColorBrush WarnBrush = new(Color.FromArgb(255, 224, 168, 76));
    private static readonly SolidColorBrush ErrBrush = new(Color.FromArgb(255, 214, 94, 86));
    private static readonly SolidColorBrush IdleBrush = new(Color.FromArgb(255, 192, 86, 74));
    private static readonly SolidColorBrush BusyBrush = new(Color.FromArgb(255, 232, 162, 74));
    private static readonly SolidColorBrush ConnectedBrush = new(Color.FromArgb(255, 92, 184, 92));

    private readonly WatchSession _session = new();
    private readonly ObservableCollection<LogEntry> _logs = new();
    private readonly ObservableCollection<LogEntry> _recentLogs = new();
    private SnapshotImage? _lastSnapshot;
    private SerialTransport? _monitor;
    private bool _connecting;
    private bool _closing;
    private bool _syncingSettings;

    public MainWindow()
    {
        InitializeComponent();
        Title = "AT32 手表 · 调试启动器";

        try
        {
            SystemBackdrop = new MicaBackdrop();
        }
        catch
        {
            // Windows 10 没有 Mica，忽略即可
        }

        ResizeToWorkArea();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBarDragArea);
        RemoveWindowChromeBorder();
        ApplySystemCaptionTheme();
        LoadTitleBarIcon();
        SetAppIcon();

        LogList.ItemsSource = _logs;
        RecentList.ItemsSource = _recentLogs;
        _session.Log += OnSessionLog;
        _session.ConnectionChanged += OnConnectionChanged;
        _session.StatsReceived += OnStatsReceived;

        ModeBox.SelectedIndex = 1;
        ApplyModeDefaults();
        InitSettingsFields();

        Nav.SelectedItem = HomeItem;
        ShowPanel("home");

        SetStatus("未连接", IdleBrush);
        AddLog("info", "调试启动器已就绪 · 默认经典 SPP，启动后自动连接手表");
        AddLog("info", "协议 A5 命令 长度 数据 异或 · 蓝牙特征 FFE1 · 串口 115200 8N1");

        RootGrid.Loaded += OnRootLoaded;
        Closed += OnWindowClosed;
    }

    private async void OnRootLoaded(object sender, RoutedEventArgs e)
    {
        RootGrid.Loaded -= OnRootLoaded;
        RemoveWindowChromeBorder();
        Nav.SelectedItem = HomeItem;
        ShowPanel("home");
        await Task.Delay(700);
        if (!_session.IsConnected && !_connecting)
        {
            Connect_Click(this, new RoutedEventArgs());
        }
    }

    private void Nav_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is NavigationViewItem { Tag: string tag })
        {
            ShowPanel(tag);
        }
    }

    private void ShowPanel(string tag)
    {
        if (HomePanel is null)
        {
            return;
        }

        HomePanel.Visibility = tag == "home" ? Visibility.Visible : Visibility.Collapsed;
        DebugPanel.Visibility = tag == "debug" ? Visibility.Visible : Visibility.Collapsed;
        ScreenPanel.Visibility = tag == "screen" ? Visibility.Visible : Visibility.Collapsed;
        LogPanel.Visibility = tag == "log" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPanel.Visibility = tag == "settings" ? Visibility.Visible : Visibility.Collapsed;
        AboutPanel.Visibility = tag == "about" ? Visibility.Visible : Visibility.Collapsed;

        if (tag == "log" && AutoScrollCheck.IsChecked == true && _logs.Count > 0)
        {
            DispatcherQueue.TryEnqueue(() => LogList.ScrollIntoView(_logs[^1]));
        }
    }

    private void InitSettingsFields()
    {
        _syncingSettings = true;
        SettingsModeBox.SelectedIndex = ModeBox.SelectedIndex;
        SettingsAddressBox.Text = AddressBox.Text;
        SettingsBaudBox.Text = BaudBox.Text;
        _syncingSettings = false;
    }

    private void ApplySettings_Click(object sender, RoutedEventArgs e)
    {
        if (_syncingSettings)
        {
            return;
        }

        if (SettingsModeBox.SelectedIndex >= 0)
        {
            ModeBox.SelectedIndex = SettingsModeBox.SelectedIndex;
        }

        string address = SettingsAddressBox.Text.Trim();
        if (address.Length > 0)
        {
            AddressBox.Text = address;
        }

        if (int.TryParse(SettingsBaudBox.Text.Trim(), out _))
        {
            BaudBox.Text = SettingsBaudBox.Text.Trim();
        }

        AddLog("info", "设置已应用到主页连接参数");
    }

    private void OpenScreen_Click(object sender, RoutedEventArgs e)
    {
        Nav.SelectedItem = ScreenItem;
    }

    private async void QuickSnap_Click(object sender, RoutedEventArgs e)
    {
        Nav.SelectedItem = ScreenItem;
        await SnapAsync();
    }

    private async void QuickTime_Click(object sender, RoutedEventArgs e) => await _session.SyncTimeAsync();

    private void QuickMessage_Click(object sender, RoutedEventArgs e)
    {
        Nav.SelectedItem = DebugItem;
        MessageBox.Focus(FocusState.Programmatic);
    }

    private async void QuickDebug_Click(object sender, RoutedEventArgs e) =>
        await _session.SendFrameAsync(Protocol.CmdDebug, new byte[] { 1 });

    /// <summary>按当前 DPI 计算窗口尺寸，保证 1180×760 的布局在小屏上也完整可见。</summary>
    private void ResizeToWorkArea()
    {
        var appWindow = AppWindow;
        uint dpi = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this));
        double scale = dpi == 0 ? 1.0 : dpi / 96.0;

        var area = DisplayArea.GetFromWindowId(appWindow.Id, DisplayAreaFallback.Primary);
        if (area is null)
        {
            appWindow.Resize(new SizeInt32(PreferredWidth, PreferredHeight));
            return;
        }

        var work = area.WorkArea;
        int maxWidth = Math.Max(820, (int)(work.Width / scale) - 24);
        int maxHeight = Math.Max(560, (int)(work.Height / scale) - 24);
        int width = Math.Min(PreferredWidth, maxWidth);
        int height = Math.Min(PreferredHeight, maxHeight);

        int pixelWidth = (int)Math.Round(width * scale);
        int pixelHeight = (int)Math.Round(height * scale);
        appWindow.Resize(new SizeInt32(pixelWidth, pixelHeight));
        appWindow.Move(new PointInt32(
            work.X + Math.Max(0, (work.Width - pixelWidth) / 2),
            work.Y + Math.Max(0, (work.Height - pixelHeight) / 2)));
    }

    private void RemoveWindowChromeBorder()
    {
        IntPtr hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);

        // DWMWA_BORDER_COLOR：用暗色边框替代系统强调色，避免粉色高亮
        int border = 0x00221F1E;
        _ = DwmSetWindowAttribute(hwnd, 34, ref border, sizeof(int));

        // DWMWA_USE_IMMERSIVE_DARK_MODE
        int dark = 1;
        _ = DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int));

        // DWMWA_WINDOW_CORNER_PREFERENCE：保留 Windows 11 圆角
        int round = 2;
        _ = DwmSetWindowAttribute(hwnd, 33, ref round, sizeof(int));
    }

    /// <summary>把系统标题栏按钮融进自定义标题栏，避免与自绘按钮重叠。</summary>
    private void ApplySystemCaptionTheme()
    {
        var titleBar = AppWindow.TitleBar;
        titleBar.ButtonBackgroundColor = Color.FromArgb(0, 0, 0, 0);
        titleBar.ButtonInactiveBackgroundColor = Color.FromArgb(0, 0, 0, 0);
        titleBar.ButtonForegroundColor = Color.FromArgb(255, 240, 240, 240);
        titleBar.ButtonInactiveForegroundColor = Color.FromArgb(255, 154, 160, 166);
        titleBar.ButtonHoverBackgroundColor = Color.FromArgb(40, 255, 255, 255);
        titleBar.ButtonHoverForegroundColor = Color.FromArgb(255, 255, 255, 255);
        titleBar.ButtonPressedBackgroundColor = Color.FromArgb(60, 255, 255, 255);
        titleBar.ButtonPressedForegroundColor = Color.FromArgb(255, 255, 255, 255);
    }

    private async void LoadTitleBarIcon()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "Assets", "app.png");
        if (!File.Exists(path))
        {
            return;
        }

        try
        {
            using var stream = File.OpenRead(path);
            var bitmap = new BitmapImage();
            await bitmap.SetSourceAsync(stream.AsRandomAccessStream());
            TitleBarIcon.Source = bitmap;
        }
        catch
        {
        }
    }

    private void SetAppIcon()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
        if (!File.Exists(path))
        {
            return;
        }

        try
        {
            AppWindow.SetIcon(path);
        }
        catch
        {
        }
    }

    private async void OnWindowClosed(object sender, WindowEventArgs args)
    {
        _closing = true;
        _session.Log -= OnSessionLog;
        _session.ConnectionChanged -= OnConnectionChanged;
        _session.StatsReceived -= OnStatsReceived;
        await StopMonitorAsync();
        await _session.DisposeAsync();
    }

    private TransportKind SelectedKind
    {
        get
        {
            string? tag = (ModeBox.SelectedItem as ComboBoxItem)?.Tag as string;
            return tag switch
            {
                "ble" => TransportKind.Ble,
                "spp" => TransportKind.Spp,
                "serial" => TransportKind.Serial,
                _ => TransportKind.Demo,
            };
        }
    }

    private void ModeBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (AddressBox is null)
        {
            return;
        }
        ApplyModeDefaults();
    }

    private void ApplyModeDefaults()
    {
        bool serial = SelectedKind == TransportKind.Serial;
        BaudLabel.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        BaudBox.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        ModeHint.Text = SelectedKind switch
        {
            TransportKind.Demo => "本地演示，无需硬件",
            TransportKind.Ble => "WinRT 直连 · FFE1 通知 · 约 3.8 秒/帧",
            TransportKind.Spp => "RFCOMM 通道 1 · 裸 socket",
            TransportKind.Serial => "CH340 / USB-TTL · 115200 8N1",
            _ => string.Empty,
        };

        switch (SelectedKind)
        {
            case TransportKind.Ble:
                AddressLabel.Text = "地址";
                AddressBox.Text = BleAddress;
                AddressBox.IsEnabled = true;
                BaudBox.IsEnabled = false;
                break;
            case TransportKind.Spp:
                AddressLabel.Text = "地址";
                AddressBox.Text = SppAddress;
                AddressBox.IsEnabled = true;
                BaudBox.IsEnabled = false;
                break;
            case TransportKind.Serial:
                AddressLabel.Text = "端口";
                AddressBox.Text = "COM7";
                AddressBox.IsEnabled = true;
                BaudBox.IsEnabled = true;
                break;
            default:
                AddressLabel.Text = "通道";
                AddressBox.Text = "本地演示";
                AddressBox.IsEnabled = false;
                BaudBox.IsEnabled = false;
                break;
        }
    }

    private async void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_connecting)
        {
            return;
        }

        _connecting = true;
        ConnectButton.IsEnabled = false;
        HeroConnectButton.IsEnabled = false;
        SetStatus("连接中", BusyBrush);
        try
        {
            int baud = int.TryParse(BaudBox.Text.Trim(), out int parsed) ? parsed : 115200;
            bool ok = await _session.ConnectAsync(SelectedKind, AddressBox.Text.Trim(), baud);
            SetStatus(ok ? $"已连接 · {_session.ConnectedName}" : "未连接", ok ? ConnectedBrush : IdleBrush);
            InfoChannel.Text = ok ? _session.ConnectedName : "—";
            if (ok)
            {
                _ = RefreshHomePreviewAsync();
            }
        }
        finally
        {
            _connecting = false;
            ConnectButton.IsEnabled = true;
            HeroConnectButton.IsEnabled = true;
        }
    }

    private async void Disconnect_Click(object sender, RoutedEventArgs e)
    {
        await _session.DisconnectAsync();
        SetStatus("未连接", IdleBrush);
        InfoChannel.Text = "—";
    }

    private void OnConnectionChanged(object? sender, bool connected)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            if (connected)
            {
                SetStatus($"已连接 · {_session.ConnectedName}", ConnectedBrush);
                InfoChannel.Text = _session.ConnectedName;
                TelemetryHint.Text = "等待遥测…";
                TelemetryDot.Opacity = 0.8;
                RamDetail.Text = "等待遥测…";
                CpuDetail.Text = "等待遥测…";
                FpsDetail.Text = "等待遥测…";
                HeroBadgeText.Text = "读取主页…";
            }
            else
            {
                SetStatus("未连接", IdleBrush);
                InfoChannel.Text = "—";
                ResetTelemetry();
                if (HeroImage.Visibility == Visibility.Visible)
                {
                    HeroBadgeText.Text = "最近画面";
                }
            }
        });
    }

    private void SetStatus(string text, Brush brush)
    {
        StatusText.Text = text;
        StatusText.Foreground = brush;
        StatusDot.Fill = brush;
    }

    private void OnStatsReceived(WatchStats stats)
    {
        DispatcherQueue.TryEnqueue(() => UpdateTelemetry(stats));
    }

    private void UpdateTelemetry(WatchStats stats)
    {
        if (stats.RamTotal > 0)
        {
            double ramPercent = Math.Clamp(stats.RamUsed * 100.0 / stats.RamTotal, 0, 100);
            RamRing.Value = ramPercent;
            RamPercent.Text = $"{ramPercent:F0}%";
            RamValue.Text = $"{stats.RamUsed / 1024.0:F1} / {stats.RamTotal / 1024.0:F1} KB";
            RamDetail.Text = $"剩余 {(stats.RamTotal - stats.RamUsed) / 1024.0:F1} KB";
        }
        else
        {
            RamRing.Value = 0;
            RamPercent.Text = "—";
            RamValue.Text = "—";
            RamDetail.Text = "固件未上报";
        }

        double cpu = stats.CpuTenths / 10.0;
        CpuRing.Value = Math.Clamp(cpu, 0, 100);
        CpuPercent.Text = $"{cpu:F0}%";
        CpuValue.Text = $"{cpu:F1} %";
        CpuDetail.Text = "1 秒采样 · 主循环";
        CpuRing.Foreground = cpu >= 80 ? ErrBrush : cpu >= 50 ? BusyBrush : ConnectedBrush;

        if (stats.Fps > 0)
        {
            FpsRing.Value = Math.Clamp(stats.Fps / 60.0 * 100, 0, 100);
            FpsPercent.Text = stats.Fps.ToString();
            FpsValue.Text = $"{stats.Fps} fps";
            FpsDetail.Text = $"{1000.0 / stats.Fps:F1} ms 帧周期";
        }
        else
        {
            FpsRing.Value = 0;
            FpsPercent.Text = "—";
            FpsValue.Text = "—";
            FpsDetail.Text = "等待采样";
        }

        TelemetryHint.Text = "1 Hz · 来自手表";
        TelemetryDot.Opacity = 1.0;
    }

    private void ResetTelemetry()
    {
        RamRing.Value = 0;
        RamPercent.Text = "—";
        RamValue.Text = "—";
        RamDetail.Text = "等待连接";
        CpuRing.Value = 0;
        CpuPercent.Text = "—";
        CpuValue.Text = "—";
        CpuDetail.Text = "等待连接";
        FpsRing.Value = 0;
        FpsPercent.Text = "—";
        FpsValue.Text = "—";
        FpsDetail.Text = "等待连接";
        TelemetryHint.Text = "未连接";
        TelemetryDot.Opacity = 0.35;
    }

    private async void PrevKey_Click(object sender, RoutedEventArgs e) => await _session.TapKeyAsync(1);

    private async void ConfirmKey_Click(object sender, RoutedEventArgs e) => await _session.TapKeyAsync(2);

    private async void NextKey_Click(object sender, RoutedEventArgs e) => await _session.TapKeyAsync(4);

    private async void LongConfirmKey_Click(object sender, RoutedEventArgs e) => await _session.TapKeyAsync(2, 700);

    private async void Brightness_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string tag } && byte.TryParse(tag, out byte level))
        {
            await _session.SendFrameAsync(Protocol.CmdBrightness, new[] { level });
        }
    }

    private async void SyncTime_Click(object sender, RoutedEventArgs e) => await _session.SyncTimeAsync();

    private async void DebugOn_Click(object sender, RoutedEventArgs e) =>
        await _session.SendFrameAsync(Protocol.CmdDebug, new byte[] { 1 });

    private async void DebugOff_Click(object sender, RoutedEventArgs e) =>
        await _session.SendFrameAsync(Protocol.CmdDebug, new byte[] { 0 });

    private async void PlayStateOn_Click(object sender, RoutedEventArgs e) =>
        await _session.SetPlayStateAsync(true);

    private async void PlayStateOff_Click(object sender, RoutedEventArgs e) =>
        await _session.SetPlayStateAsync(false);

    private async void Perf_Click(object sender, RoutedEventArgs e) =>
        await _session.RequestPerfAsync();

    private async void Sensor_Click(object sender, RoutedEventArgs e)
    {
        SensorOutput.Text = "读取中…";
        string? line = await _session.QuerySensorAsync();
        SensorOutput.Text = line ?? "无回应";
    }

    private async void UnpairBle_Click(object sender, RoutedEventArgs e)
    {
        string address = AddressBox.Text.Trim();
        if (!address.Contains(':'))
        {
            address = BleAddress;
        }
        await _session.UnpairBleAsync(address);
    }

    private async void Monitor_Click(object sender, RoutedEventArgs e)
    {
        if (_monitor is not null)
        {
            await StopMonitorAsync();
            return;
        }

        string port = MonitorPortBox.Text.Trim();
        if (port.Length == 0)
        {
            await ShowErrorAsync("串口监听", "先填 COM 口，例如 COM7");
            return;
        }

        int baud = int.TryParse(MonitorBaudBox.Text.Trim(), out int parsed) ? parsed : 115200;
        var monitor = new SerialTransport(port, baud);
        monitor.DataReceived += OnMonitorData;
        monitor.ConnectionChanged += OnMonitorConnectionChanged;
        try
        {
            await monitor.ConnectAsync();
            _monitor = monitor;
            MonitorButton.Content = "停止监听";
            MonitorPortBox.IsEnabled = false;
            MonitorBaudBox.IsEnabled = false;
            AddLog("info", $"开始监听 {port} @ {baud}");
        }
        catch (Exception ex)
        {
            monitor.DataReceived -= OnMonitorData;
            monitor.ConnectionChanged -= OnMonitorConnectionChanged;
            await monitor.DisposeAsync();
            AddLog("err", $"监听 {port} 打开失败：{ex.Message}");
        }
    }

    private void OnMonitorData(object? sender, byte[] data)
    {
        if (_closing)
        {
            return;
        }
        DispatcherQueue.TryEnqueue(() => AddLog("rx", "[MON] " + Protocol.Hex(data)));
    }

    private void OnMonitorConnectionChanged(object? sender, bool connected)
    {
        if (connected || _closing)
        {
            return;
        }
        DispatcherQueue.TryEnqueue(() =>
        {
            AddLog("warn", "监听口已断开");
            ResetMonitorUi();
        });
    }

    private async Task StopMonitorAsync()
    {
        var monitor = _monitor;
        if (monitor is null)
        {
            return;
        }

        _monitor = null;
        monitor.DataReceived -= OnMonitorData;
        monitor.ConnectionChanged -= OnMonitorConnectionChanged;
        await monitor.DisposeAsync();
        ResetMonitorUi();
        AddLog("info", "监听已停止");
    }

    private void ResetMonitorUi()
    {
        MonitorButton.Content = "开始监听";
        MonitorPortBox.IsEnabled = true;
        MonitorBaudBox.IsEnabled = true;
    }

    private async void Message_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string tag } || !byte.TryParse(tag, out byte slot))
        {
            return;
        }

        byte[] text = Protocol.FitText(MessageBox.Text ?? string.Empty, 23);
        var body = new byte[1 + text.Length];
        body[0] = slot;
        Array.Copy(text, 0, body, 1, text.Length);
        await _session.SendFrameAsync(Protocol.CmdMessage, body);
    }

    private async void Lyric_Click(object sender, RoutedEventArgs e)
    {
        byte[] body = Protocol.FitText(MessageBox.Text ?? string.Empty, 27);
        await _session.SendFrameAsync(Protocol.CmdLyric, body);
    }

    private async void RawCommand_Click(object sender, RoutedEventArgs e)
    {
        string text = RawCommandBox.Text.Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            text = text[2..];
        }

        if (!byte.TryParse(text, System.Globalization.NumberStyles.HexNumber, null, out byte cmd))
        {
            await ShowErrorAsync("原始命令", "请输入一个十六进制命令字节，例如 06");
            return;
        }

        await _session.SendFrameAsync(cmd);
    }

    private async void CustomSend_Click(object sender, RoutedEventArgs e)
    {
        string text = CustomBox.Text ?? string.Empty;
        byte[] data;

        if (HexCheck.IsChecked == true)
        {
            string cleaned = text
                .Replace("0x", string.Empty, StringComparison.OrdinalIgnoreCase)
                .Replace("0X", string.Empty, StringComparison.OrdinalIgnoreCase)
                .Replace(",", " ");
            cleaned = string.Concat(cleaned.Where(c => !char.IsWhiteSpace(c)));

            if (cleaned.Length % 2 != 0)
            {
                await ShowErrorAsync("自定义发送", "HEX 需要偶数个十六进制字符");
                return;
            }

            try
            {
                data = Convert.FromHexString(cleaned);
            }
            catch (FormatException)
            {
                await ShowErrorAsync("自定义发送", "HEX 格式不对，例如：A5 7F 00 DA");
                return;
            }
        }
        else
        {
            data = Encoding.UTF8.GetBytes(text);
        }

        if (CrlfCheck.IsChecked == true)
        {
            data = data.Concat(new byte[] { 0x0D, 0x0A }).ToArray();
        }

        if (data.Length == 0)
        {
            return;
        }

        if (CustomTargetBox.SelectedIndex == 1)
        {
            var monitor = _monitor;
            if (monitor is null || !monitor.IsConnected)
            {
                await ShowErrorAsync("自定义发送", "监听口未打开");
                return;
            }

            try
            {
                await monitor.SendAsync(data);
                AddLog("tx", "[MON] TX " + Protocol.Hex(data));
            }
            catch (Exception ex)
            {
                AddLog("err", $"监听口发送失败：{ex.Message}");
            }
            return;
        }

        await _session.SendRawAsync(data);
    }

    private void CustomClear_Click(object sender, RoutedEventArgs e)
    {
        CustomBox.Text = string.Empty;
    }

    private async void Snap_Click(object sender, RoutedEventArgs e)
    {
        await SnapAsync();
    }

    private async Task SnapAsync()
    {
        SnapButton.IsEnabled = false;
        SaveButton.IsEnabled = false;
        SnapProgress.Value = 0;
        SnapProgress.Visibility = Visibility.Visible;
        SnapInfo.Text = "抓屏中…";

        var progress = new Progress<int>(value => SnapProgress.Value = value);
        try
        {
            var image = await _session.RequestSnapshotAsync(progress);
            if (image is null)
            {
                SnapInfo.Text = "抓屏失败";
                return;
            }

            _lastSnapshot = image;
            ShowSnapshot(image);
            SaveButton.IsEnabled = true;
            SnapInfo.Text = $"{image.Width}×{image.Height} · {DateTime.Now:HH:mm:ss}";
        }
        catch (Exception ex)
        {
            AddLog("err", $"抓屏异常：{ex.Message}");
            SnapInfo.Text = "抓屏失败";
        }
        finally
        {
            SnapButton.IsEnabled = true;
            SnapProgress.Value = 0;
            SnapProgress.Visibility = Visibility.Collapsed;
        }
    }

    private void ShowSnapshot(SnapshotImage image)
    {
        var bitmap = CreateBitmap(image, 2);
        ScreenImage.Source = bitmap;
        HeroImage.Source = bitmap;
        ScreenImage.Visibility = Visibility.Visible;
        ScreenPlaceholder.Visibility = Visibility.Collapsed;
        HeroImage.Visibility = Visibility.Visible;
        HeroPlaceholder.Visibility = Visibility.Collapsed;
        HeroBadgeText.Text = _session.ConnectedName == "演示" ? "演示画面" : "实时画面";
    }

    private async Task RefreshHomePreviewAsync()
    {
        await Task.Delay(400);
        if (_closing || !_session.IsConnected)
        {
            return;
        }
        await SnapAsync();
    }

    private static WriteableBitmap CreateBitmap(SnapshotImage image, int scale)
    {
        byte[] scaled = SnapshotDecoder.ScaleNearest(image, scale);
        var bitmap = new WriteableBitmap(image.Width * scale, image.Height * scale);
        using (var stream = bitmap.PixelBuffer.AsStream())
        {
            stream.Write(scaled, 0, scaled.Length);
        }
        return bitmap;
    }

    private async void SavePng_Click(object sender, RoutedEventArgs e)
    {
        if (_lastSnapshot is null)
        {
            await ShowErrorAsync("保存", "先抓一帧");
            return;
        }

        var picker = new FileSavePicker
        {
            SuggestedStartLocation = PickerLocationId.PicturesLibrary,
            SuggestedFileName = $"watch-{DateTime.Now:yyyyMMdd-HHmmss}",
        };
        picker.FileTypeChoices.Add("PNG 图片", new List<string> { ".png" });
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));

        var file = await picker.PickSaveFileAsync();
        if (file is null)
        {
            return;
        }

        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        var bitmap = SoftwareBitmap.CreateCopyFromBuffer(
            _lastSnapshot.Bgra.AsBuffer(),
            BitmapPixelFormat.Bgra8,
            _lastSnapshot.Width,
            _lastSnapshot.Height,
            BitmapAlphaMode.Ignore);
        encoder.SetSoftwareBitmap(bitmap);
        await encoder.FlushAsync();
        AddLog("ok", $"已保存 {file.Path}");
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e)
    {
        _logs.Clear();
        _recentLogs.Clear();
    }

    private void OnSessionLog(string level, string message)
    {
        if (_closing)
        {
            return;
        }
        DispatcherQueue.TryEnqueue(() => AddLog(level, message));
    }

    private void AddLog(string level, string message)
    {
        var (tag, brush) = level switch
        {
            "tx" => ("TX", TxBrush),
            "rx" => ("RX", RxBrush),
            "ok" => ("OK", OkBrush),
            "warn" => ("WARN", WarnBrush),
            "err" => ("ERR", ErrBrush),
            _ => ("INFO", InfoBrush),
        };

        var entry = new LogEntry
        {
            Time = DateTime.Now.ToString("HH:mm:ss"),
            Tag = tag,
            Message = message,
            TagBrush = brush,
        };

        _logs.Add(entry);
        _recentLogs.Insert(0, entry);
        while (_recentLogs.Count > 6)
        {
            _recentLogs.RemoveAt(_recentLogs.Count - 1);
        }

        while (_logs.Count > 1500)
        {
            _logs.RemoveAt(0);
        }

        if (AutoScrollCheck.IsChecked == true && _logs.Count > 0 && LogPanel.Visibility == Visibility.Visible)
        {
            LogList.ScrollIntoView(_logs[^1]);
        }
    }

    private async Task ShowErrorAsync(string title, string message)
    {
        var dialog = new ContentDialog
        {
            Title = title,
            Content = message,
            CloseButtonText = "知道了",
            XamlRoot = RootGrid.XamlRoot,
        };
        await dialog.ShowAsync();
    }
}
