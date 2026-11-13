namespace At32Debugger.Services;

/// <summary>演示通道用的 128×160 表盘画面，纯 BGRA 像素。</summary>
public static class DemoFrame
{
    public static byte[] Build(int width, int height)
    {
        var px = new byte[width * height * 4];

        // 背景：由上到下的深色渐变
        for (int y = 0; y < height; y++)
        {
            double t = height <= 1 ? 0 : y / (double)(height - 1);
            byte r = (byte)(16 + (6 - 16) * t);
            byte g = (byte)(21 + (9 - 21) * t);
            byte b = (byte)(28 + (12 - 28) * t);
            for (int x = 0; x < width; x++)
            {
                int o = (y * width + x) * 4;
                px[o] = b;
                px[o + 1] = g;
                px[o + 2] = r;
                px[o + 3] = 0xFF;
            }
        }

        int cx = width / 2;
        int cy = 74;

        Ring(px, width, height, cx, cy, 54, 1, 232, 162, 74);
        Ring(px, width, height, cx, cy, 49, 1, 44, 49, 57);

        for (int i = 0; i < 12; i++)
        {
            bool major = i % 3 == 0;
            double rad = (i * 30 - 90) * Math.PI / 180.0;
            int r0 = major ? 43 : 46;
            int x0 = cx + (int)Math.Round(r0 * Math.Cos(rad));
            int y0 = cy + (int)Math.Round(r0 * Math.Sin(rad));
            int x1 = cx + (int)Math.Round(49 * Math.Cos(rad));
            int y1 = cy + (int)Math.Round(49 * Math.Sin(rad));
            if (major)
            {
                Line(px, width, height, x0, y0, x1, y1, 216, 222, 230, 2);
            }
            else
            {
                Line(px, width, height, x0, y0, x1, y1, 92, 100, 110, 1);
            }
        }

        var now = DateTime.Now;
        double hourDeg = (now.Hour % 12) * 30 + now.Minute * 0.5;
        double minuteDeg = now.Minute * 6 + now.Second * 0.1;
        double secondDeg = now.Second * 6;

        Hand(px, width, height, cx, cy, hourDeg, 27, 4, 242, 244, 246);
        Hand(px, width, height, cx, cy, minuteDeg, 38, 3, 242, 244, 246);
        Hand(px, width, height, cx, cy, secondDeg, 43, 1, 232, 162, 74);

        FillCircle(px, width, height, cx, cy, 3, 232, 162, 74);

        // 底部页面指示点
        for (int i = 0; i < 3; i++)
        {
            bool active = i == 1;
            FillCircle(px, width, height, cx + (i - 1) * 10, 150, active ? 3 : 2,
                active ? (byte)232 : (byte)90,
                active ? (byte)162 : (byte)96,
                active ? (byte)74 : (byte)108);
        }

        return px;
    }

    private static void Hand(byte[] px, int w, int h, int cx, int cy, double deg, int length, int thickness,
        byte r, byte g, byte b)
    {
        double rad = (deg - 90) * Math.PI / 180.0;
        int x = cx + (int)Math.Round(length * Math.Cos(rad));
        int y = cy + (int)Math.Round(length * Math.Sin(rad));
        Line(px, w, h, cx, cy, x, y, r, g, b, thickness);
    }

    private static void Set(byte[] px, int w, int h, int x, int y, byte r, byte g, byte b)
    {
        if ((uint)x >= (uint)w || (uint)y >= (uint)h)
        {
            return;
        }
        int o = (y * w + x) * 4;
        px[o] = b;
        px[o + 1] = g;
        px[o + 2] = r;
        px[o + 3] = 0xFF;
    }

    private static void FillCircle(byte[] px, int w, int h, int cx, int cy, int radius, byte r, byte g, byte b)
    {
        int r2 = radius * radius;
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y <= r2)
                {
                    Set(px, w, h, cx + x, cy + y, r, g, b);
                }
            }
        }
    }

    private static void Ring(byte[] px, int w, int h, int cx, int cy, int radius, int thickness,
        byte r, byte g, byte b)
    {
        int outer = radius + thickness;
        int inner = radius - thickness;
        int outer2 = outer * outer;
        int inner2 = inner * inner;
        for (int y = -outer; y <= outer; y++)
        {
            for (int x = -outer; x <= outer; x++)
            {
                int d = x * x + y * y;
                if (d <= outer2 && d >= inner2)
                {
                    Set(px, w, h, cx + x, cy + y, r, g, b);
                }
            }
        }
    }

    private static void Line(byte[] px, int w, int h, int x0, int y0, int x1, int y1,
        byte r, byte g, byte b, int thickness)
    {
        int dx = x1 - x0;
        int dy = y1 - y0;
        int steps = Math.Max(Math.Abs(dx), Math.Abs(dy));
        if (steps == 0)
        {
            FillCircle(px, w, h, x0, y0, Math.Max(1, thickness), r, g, b);
            return;
        }

        int brush = Math.Max(0, thickness / 2);
        for (int i = 0; i <= steps; i++)
        {
            int x = x0 + (int)Math.Round(dx * (i / (double)steps));
            int y = y0 + (int)Math.Round(dy * (i / (double)steps));
            if (brush == 0)
            {
                Set(px, w, h, x, y, r, g, b);
            }
            else
            {
                FillCircle(px, w, h, x, y, brush, r, g, b);
            }
        }
    }
}
