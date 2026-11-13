using System.Text;

namespace At32Debugger.Services;

/// <summary>手表蓝牙调试协议：A5 命令 长度 数据... 异或（异或从 A5 算到数据末尾）。</summary>
public static class Protocol
{
    public const byte Mark = 0xA5;

    public const byte CmdTime = 0x01;
    public const byte CmdMessage = 0x02;
    public const byte CmdLyric = 0x03;
    public const byte CmdState = 0x04;
    public const byte CmdBrightness = 0x05;
    public const byte CmdSnapshot = 0x06;
    public const byte CmdKey = 0x07;
    public const byte CmdDebug = 0x08;
    public const byte CmdPerf = 0x09;
    public const byte CmdHome = 0x0A;
    public const byte CmdCtrl = 0x10;
    public const byte CmdImage = 0x11;
    public const byte CmdStats = 0x12;
    public const byte CmdSensor = 0x13;
    public const byte CmdNop = 0x7F;

    /// <summary>按 UTF-8 字符边界截断，避免把汉字切成半个。</summary>
    public static byte[] FitText(string text, int limit)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        if (bytes.Length <= limit)
        {
            return bytes;
        }

        int end = limit;
        while (end > 0 && (bytes[end] & 0xC0) == 0x80)
        {
            end--;
        }
        return bytes[..end];
    }

    public static byte[] Frame(byte cmd, params byte[] payload)
    {
        payload ??= Array.Empty<byte>();
        var frame = new byte[payload.Length + 4];
        frame[0] = Mark;
        frame[1] = cmd;
        frame[2] = (byte)payload.Length;
        payload.CopyTo(frame, 3);

        byte xor = 0;
        for (int i = 0; i < frame.Length - 1; i++)
        {
            xor ^= frame[i];
        }
        frame[^1] = xor;
        return frame;
    }

    public static string Hex(ReadOnlySpan<byte> data, int limit = 64)
    {
        int n = Math.Min(data.Length, limit);
        var sb = new StringBuilder(n * 4 + 24);
        for (int i = 0; i < n; i++)
        {
            if (i > 0)
            {
                sb.Append(' ');
            }
            sb.Append(data[i].ToString("X2"));
        }

        sb.Append("  |");
        for (int i = 0; i < n; i++)
        {
            byte b = data[i];
            sb.Append(b >= 32 && b < 127 ? (char)b : '.');
        }
        sb.Append('|');

        if (data.Length > limit)
        {
            sb.Append(" ...(+").Append(data.Length - limit).Append(')');
        }
        return sb.ToString();
    }
}
