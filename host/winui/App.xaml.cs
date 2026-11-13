using System.IO;
using Microsoft.UI.Xaml;

namespace At32Debugger;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        UnhandledException += (_, e) => WriteLog("XAML UnhandledException: " + e.Exception);
        AppDomain.CurrentDomain.UnhandledException += (_, e) => WriteLog("AppDomain: " + e.ExceptionObject);
        TaskScheduler.UnobservedTaskException += (_, e) => WriteLog("Task: " + e.Exception);
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        try
        {
            _window = new MainWindow();
            _window.Activate();
        }
        catch (Exception ex)
        {
            WriteLog("OnLaunched: " + ex);
            throw;
        }
    }

    private static void WriteLog(string text)
    {
        try
        {
            File.AppendAllText(
                Path.Combine(AppContext.BaseDirectory, "startup-error.log"),
                $"{DateTime.Now:HH:mm:ss} {text}{Environment.NewLine}");
        }
        catch
        {
            // 日志写不进去就算了
        }
    }
}

