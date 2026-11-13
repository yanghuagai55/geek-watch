namespace At32Debugger.Services;

public sealed record SnapshotImage(int Width, int Height, byte[] Bgra);

/// <summary>把固件回传的 RGB565（大端）转成 BGRA，并做整数倍最近邻放大。</summary>
public static class SnapshotDecoder
{
    public static SnapshotImage? Decode(byte[] payload)
    {
        if (payload.Length < 4)
        {
            return null;
        }

        int width = payload[0] | (payload[1] << 8);
        int height = payload[2] | (payload[3] << 8);
        if (width <= 0 || height <= 0 || payload.Length < 4 + width * height * 2)
        {
            return null;
        }

        var bgra = new byte[width * height * 4];
        for (int i = 0; i < width * height; i++)
        {
            int hi = payload[4 + i * 2];
            int lo = payload[5 + i * 2];
            int v = (hi << 8) | lo;
            int r = (v >> 11) & 0x1F;
            int g = (v >> 5) & 0x3F;
            int b = v & 0x1F;
            byte r8 = (byte)((r << 3) | (r >> 2));
            byte g8 = (byte)((g << 2) | (g >> 4));
            byte b8 = (byte)((b << 3) | (b >> 2));

            int o = i * 4;
            bgra[o] = b8;
            bgra[o + 1] = g8;
            bgra[o + 2] = r8;
            bgra[o + 3] = 0xFF;
        }
        return new SnapshotImage(width, height, bgra);
    }

    public static byte[] ScaleNearest(SnapshotImage image, int scale)
    {
        int w = image.Width * scale;
        int h = image.Height * scale;
        var dst = new byte[w * h * 4];
        var src = image.Bgra;

        for (int y = 0; y < h; y++)
        {
            int sy = y / scale;
            for (int x = 0; x < w; x++)
            {
                int sx = x / scale;
                int si = (sy * image.Width + sx) * 4;
                int di = (y * w + x) * 4;
                dst[di] = src[si];
                dst[di + 1] = src[si + 1];
                dst[di + 2] = src[si + 2];
                dst[di + 3] = src[si + 3];
            }
        }
        return dst;
    }
}
