using Microsoft.UI.Xaml.Media;

namespace At32Debugger.Services;

public sealed class LogEntry
{
    public required string Time { get; init; }

    public required string Tag { get; init; }

    public required string Message { get; init; }

    public required Brush TagBrush { get; init; }
}
