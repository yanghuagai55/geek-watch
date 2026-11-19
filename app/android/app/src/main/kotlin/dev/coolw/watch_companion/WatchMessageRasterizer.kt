package dev.coolw.watch_companion

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface

/**
 * 消息页点阵：
 *  - 列表 128×(N*22)（每行 352 字节），由手机排版，绕开手表 6×12 小字库缺字出方框的问题；
 *    手表一级卡片可见裁剪宽度为 89px（ui_app.c clip_w），列表行内容严格落在 0..88px 内；
 *  - 详情 128×160（整屏 2560 字节），块 0..19。
 */
object WatchMessageRasterizer {
    const val LIST_W = 128
    const val LIST_H = 112
    const val LIST_CHUNKS = LIST_W * LIST_H / 8 / 128

    const val DETAIL_W = 128
    const val DETAIL_H = 160
    const val DETAIL_CHUNKS = DETAIL_W * DETAIL_H / 8 / 128

    data class Item(
        val app: String,
        val title: String,
        val text: String,
        val categoryId: String = "",
        val categoryLabel: String = "",
    )

    private const val ROW_H = 22f
    private const val MARGIN = 4f
    private const val CARD_VISIBLE_W = 88f
    private const val ICON_SIZE = 14f

    private val namePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 11.5f
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val headerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 12f
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val detailPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        typeface = Typeface.SANS_SERIF
    }

    private val iconStrokePaint = Paint().apply {
        color = Color.WHITE
        style = Paint.Style.STROKE
        strokeWidth = 1.6f
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        isAntiAlias = false
    }

    private val iconFillPaint = Paint().apply {
        color = Color.WHITE
        style = Paint.Style.FILL
        isAntiAlias = false
    }

    @Synchronized
    fun list(items: List<Item>): ByteArray {
        val picked = items.take(20)
        val rawBytes = maxOf(1, picked.size) * 22 * (LIST_W / 8)
        val chunks = (rawBytes + 127) / 128
        val height = chunks * 8
        val bitmap = Bitmap.createBitmap(LIST_W, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)

        picked.forEachIndexed { index, item ->
            drawRowContent(canvas, index * ROW_H, item)
        }
        return pack(bitmap)
    }

    /** 渲染单条消息列表行 128×22（352 字节），用于 0x2D 单条增量插入动态 ROM。 */
    @Synchronized
    fun row(item: Item): ByteArray {
        val bitmap = Bitmap.createBitmap(LIST_W, ROW_H.toInt(), Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)
        drawRowContent(canvas, 0f, item)
        return pack(bitmap)
    }

    private fun drawRowContent(canvas: Canvas, top: Float, item: Item) {
        val catId = item.categoryId.trim()
        val hasIcon = catId.isNotEmpty()
        val textLeft = if (hasIcon) {
            drawCategoryIcon(canvas, catId, 1f, top + 4f, ICON_SIZE)
            18f
        } else {
            2f
        }
        val maxWidth = (CARD_VISIBLE_W - textLeft).coerceAtLeast(16f)
        val appText = item.app.trim()
        val catLabel = item.categoryLabel.trim()
        val heading = when {
            catLabel.isNotEmpty() && appText.isNotEmpty() && !appText.startsWith(catLabel) ->
                "$catLabel·$appText"
            appText.isNotEmpty() -> appText
            else -> preview(item).take(8)
        }
        if (heading.isEmpty()) return
        val metrics = namePaint.fontMetrics
        val baseline = top + (ROW_H - (metrics.descent - metrics.ascent)) / 2f - metrics.ascent
        canvas.drawText(
            ellipsize(heading, namePaint, maxWidth),
            textLeft,
            baseline,
            namePaint,
        )
    }

    @Synchronized
    fun detail(item: Item?): ByteArray {
        val bitmap = Bitmap.createBitmap(DETAIL_W, DETAIL_H, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)

        if (item == null) {
            drawEmpty(canvas)
            return pack(bitmap)
        }

        val catId = item.categoryId.trim()
        val catLabel = item.categoryLabel.trim()
        val appName = item.app.trim()
        val headerLabel = when {
            catLabel.isNotEmpty() && appName.isNotEmpty() && !appName.startsWith(catLabel) ->
                "$catLabel · $appName"
            appName.isNotEmpty() -> appName
            catLabel.isNotEmpty() -> catLabel
            else -> ""
        }

        var bodyTop = 3f
        if (headerLabel.isNotEmpty() || catId.isNotEmpty()) {
            val textX = if (catId.isNotEmpty()) {
                drawCategoryIcon(canvas, catId, MARGIN, 4f, ICON_SIZE)
                MARGIN + ICON_SIZE + 4f
            } else {
                MARGIN
            }
            if (headerLabel.isNotEmpty()) {
                val hm = headerPaint.fontMetrics
                val hb = 4f + (ICON_SIZE - (hm.descent - hm.ascent)) / 2f - hm.ascent
                canvas.drawText(
                    ellipsize(headerLabel, headerPaint, DETAIL_W - textX - MARGIN),
                    textX,
                    hb,
                    headerPaint,
                )
            }
            canvas.drawLine(MARGIN, 22f, DETAIL_W - MARGIN, 22f, iconFillPaint)
            bodyTop = 26f
        }

        val text = detailBodyText(item, headerLabel)
        if (text.isEmpty()) {
            if (bodyTop <= 4f) drawEmpty(canvas)
            return pack(bitmap)
        }

        val maxWidth = DETAIL_W - MARGIN * 2
        val maxBodyH = DETAIL_H - bodyTop - 3f
        var chosenSize = 9.5f
        var chosenLines: List<String> = emptyList()
        var size = 15f
        while (size >= 9.5f) {
            detailPaint.textSize = size
            val lines = wrap(text, detailPaint, maxWidth)
            val lineHeight = size + 2.5f
            val maxLines = (maxBodyH / lineHeight).toInt().coerceAtLeast(1)
            if (lines.size <= maxLines) {
                chosenSize = size
                chosenLines = lines
                break
            }
            chosenSize = size
            chosenLines = lines
            size -= 1f
        }

        detailPaint.textSize = chosenSize
        val lineHeight = chosenSize + 2.5f
        val maxLines = (maxBodyH / lineHeight).toInt().coerceAtLeast(1)
        val lines = if (chosenLines.size > maxLines) {
            chosenLines.take(maxLines).toMutableList().also { kept ->
                val last = kept.lastOrNull().orEmpty()
                kept[kept.size - 1] = ellipsize("$last…", detailPaint, maxWidth)
            }
        } else {
            chosenLines
        }
        val metrics = detailPaint.fontMetrics
        var baseline = bodyTop - metrics.ascent
        for (line in lines) {
            canvas.drawText(line, MARGIN, baseline, detailPaint)
            baseline += lineHeight
        }
        return pack(bitmap)
    }

    private fun detailBodyText(item: Item, headerLabel: String): String {
        val title = item.title.trim()
        val body = item.text.trim().replace("\r", "")
        val parts = mutableListOf<String>()
        if (title.isNotEmpty() && title != headerLabel && title != item.app.trim()) {
            parts.add(title)
        }
        if (body.isNotEmpty() && body != title) {
            parts.add(body)
        }
        if (parts.isEmpty() && title.isNotEmpty()) {
            parts.add(title)
        }
        return parts.joinToString("\n")
    }

    private fun preview(item: Item): String {
        val title = item.title.trim()
        val text = item.text.trim()
        return when {
            title.isEmpty() -> text.replace('\n', ' ')
            text.isEmpty() || text == title -> title.replace('\n', ' ')
            else -> "$title ${text.replace('\n', ' ')}"
        }
    }

    private fun drawEmpty(canvas: Canvas) {
        detailPaint.textSize = 16f
        val text = "无消息"
        val width = detailPaint.measureText(text)
        canvas.drawText(text, (DETAIL_W - width) / 2f, DETAIL_H / 2f + 6f, detailPaint)
    }

    private fun drawCategoryIcon(
        canvas: Canvas,
        categoryId: String,
        x: Float,
        y: Float,
        s: Float,
    ) {
        val cx = x + s / 2f
        val cy = y + s / 2f
        when (categoryId) {
            WatchCategory.CHAT.id -> {
                // 对话气泡
                canvas.drawRoundRect(
                    RectF(x + 1f, y + 1.5f, x + s - 1f, y + s - 3.5f),
                    3f,
                    3f,
                    iconStrokePaint,
                )
                val tail = Path().apply {
                    moveTo(x + 4f, y + s - 3.5f)
                    lineTo(x + 3f, y + s - 0.5f)
                    lineTo(x + 7.5f, y + s - 3.5f)
                }
                canvas.drawPath(tail, iconStrokePaint)
            }
            WatchCategory.CALL.id -> {
                // 电话听筒折线
                val phone = Path().apply {
                    moveTo(x + 2.5f, y + 2f)
                    lineTo(x + 5.5f, y + 2f)
                    lineTo(x + 6f, y + 5.5f)
                    lineTo(x + 4.5f, y + 6.5f)
                    lineTo(x + 7.5f, y + 9.5f)
                    lineTo(x + 8.5f, y + 8f)
                    lineTo(x + 12f, y + 8.5f)
                    lineTo(x + 12f, y + 11.5f)
                    lineTo(x + 9f, y + 12f)
                    lineTo(x + 2f, y + 5f)
                    close()
                }
                canvas.drawPath(phone, iconStrokePaint)
            }
            WatchCategory.SMS.id -> {
                // 短信气泡 + 横线
                canvas.drawRoundRect(
                    RectF(x + 1f, y + 2f, x + s - 1f, y + s - 2f),
                    2.5f,
                    2.5f,
                    iconStrokePaint,
                )
                canvas.drawLine(x + 3.5f, y + 5.5f, x + s - 3.5f, y + 5.5f, iconStrokePaint)
                canvas.drawLine(x + 3.5f, y + 8.5f, x + s - 5.5f, y + 8.5f, iconStrokePaint)
            }
            WatchCategory.EMAIL.id -> {
                // 信封
                canvas.drawRect(RectF(x + 1f, y + 2.5f, x + s - 1f, y + s - 2.5f), iconStrokePaint)
                val flap = Path().apply {
                    moveTo(x + 1.5f, y + 3f)
                    lineTo(cx, y + 8f)
                    lineTo(x + s - 1.5f, y + 3f)
                }
                canvas.drawPath(flap, iconStrokePaint)
            }
            WatchCategory.PAY.id -> {
                // 圆框 + ¥ 符号
                canvas.drawCircle(cx, cy, s / 2f - 1f, iconStrokePaint)
                canvas.drawLine(cx - 2.5f, y + 3.5f, cx, y + 6.5f, iconStrokePaint)
                canvas.drawLine(cx + 2.5f, y + 3.5f, cx, y + 6.5f, iconStrokePaint)
                canvas.drawLine(cx, y + 6.5f, cx, y + 11f, iconStrokePaint)
                canvas.drawLine(cx - 2.5f, y + 8f, cx + 2.5f, y + 8f, iconStrokePaint)
            }
            WatchCategory.ORDER.id -> {
                // 包裹盒
                canvas.drawRect(RectF(x + 1.5f, y + 2.5f, x + s - 1.5f, y + s - 1.5f), iconStrokePaint)
                canvas.drawLine(x + 1.5f, y + 6f, x + s - 1.5f, y + 6f, iconStrokePaint)
                canvas.drawLine(cx, y + 2.5f, cx, y + 6f, iconStrokePaint)
            }
            WatchCategory.ALARM.id -> {
                // 闹钟
                canvas.drawCircle(cx, cy + 0.5f, 4.8f, iconStrokePaint)
                canvas.drawLine(x + 2f, y + 2.5f, x + 4f, y + 1f, iconStrokePaint)
                canvas.drawLine(x + s - 2f, y + 2.5f, x + s - 4f, y + 1f, iconStrokePaint)
                canvas.drawLine(cx, cy + 0.5f, cx, cy - 2f, iconStrokePaint)
                canvas.drawLine(cx, cy + 0.5f, cx + 2.2f, cy + 0.5f, iconStrokePaint)
            }
            WatchCategory.SCHEDULE.id -> {
                // 日历页
                canvas.drawRect(RectF(x + 1.5f, y + 2.5f, x + s - 1.5f, y + s - 1.5f), iconStrokePaint)
                canvas.drawLine(x + 4.5f, y + 1f, x + 4.5f, y + 3.5f, iconStrokePaint)
                canvas.drawLine(x + s - 4.5f, y + 1f, x + s - 4.5f, y + 3.5f, iconStrokePaint)
                canvas.drawLine(x + 1.5f, y + 6f, x + s - 1.5f, y + 6f, iconStrokePaint)
            }
            WatchCategory.NAV.id -> {
                // 定位针
                canvas.drawCircle(cx, y + 5.5f, 3.8f, iconStrokePaint)
                val tip = Path().apply {
                    moveTo(cx - 2.5f, y + 8.5f)
                    lineTo(cx, y + s - 1f)
                    lineTo(cx + 2.5f, y + 8.5f)
                }
                canvas.drawPath(tip, iconStrokePaint)
            }
            WatchCategory.MEDIA.id -> {
                // 音符
                canvas.drawCircle(x + 4.5f, y + s - 3f, 2f, iconFillPaint)
                canvas.drawCircle(x + s - 3.5f, y + s - 4f, 2f, iconFillPaint)
                canvas.drawLine(x + 6f, y + s - 3f, x + 6f, y + 3f, iconStrokePaint)
                canvas.drawLine(x + s - 2f, y + s - 4f, x + s - 2f, y + 2f, iconStrokePaint)
                canvas.drawLine(x + 6f, y + 3f, x + s - 2f, y + 2f, iconStrokePaint)
            }
            WatchCategory.NEWS.id -> {
                // 资讯条目线框
                canvas.drawRect(RectF(x + 1.5f, y + 2f, x + s - 1.5f, y + s - 2f), iconStrokePaint)
                canvas.drawLine(x + 3.5f, y + 5.5f, x + s - 3.5f, y + 5.5f, iconStrokePaint)
                canvas.drawLine(x + 3.5f, y + 8.5f, x + s - 5f, y + 8.5f, iconStrokePaint)
            }
            WatchCategory.PROMO.id -> {
                // 喇叭/标签
                val tag = Path().apply {
                    moveTo(x + 2f, y + 7f)
                    lineTo(x + 7f, y + 2f)
                    lineTo(x + s - 2f, y + 2f)
                    lineTo(x + s - 2f, y + 7f)
                    lineTo(x + 7f, y + s - 2f)
                    close()
                }
                canvas.drawPath(tag, iconStrokePaint)
                canvas.drawCircle(x + s - 4.5f, y + 4.5f, 1f, iconFillPaint)
            }
            WatchCategory.SYSTEM.id -> {
                // 盾牌/系统外框
                canvas.drawCircle(cx, cy, 4.5f, iconStrokePaint)
                canvas.drawCircle(cx, cy, 1.5f, iconFillPaint)
            }
            else -> {
                // 通知铃铛
                val bell = Path().apply {
                    moveTo(x + 3f, y + s - 4f)
                    lineTo(x + 4f, y + 4.5f)
                    lineTo(cx, y + 2f)
                    lineTo(x + s - 4f, y + 4.5f)
                    lineTo(x + s - 3f, y + s - 4f)
                    close()
                }
                canvas.drawPath(bell, iconStrokePaint)
                canvas.drawLine(cx - 1.5f, y + s - 1.8f, cx + 1.5f, y + s - 1.8f, iconStrokePaint)
            }
        }
    }

    private fun ellipsize(text: String, paint: Paint, maxWidth: Float): String {
        if (paint.measureText(text) <= maxWidth) return text
        var end = text.length
        while (end > 0 && paint.measureText(text.substring(0, end) + "…") > maxWidth) {
            end--
        }
        return text.substring(0, end) + "…"
    }

    /** 中文逐字、日文假名与英文按词切，保留换行。 */
    private fun wrap(text: String, paint: Paint, maxWidth: Float): List<String> {
        val lines = mutableListOf<String>()
        for (paragraph in text.split('\n')) {
            val current = StringBuilder()
            for (token in tokenize(paragraph)) {
                if (token == " ") {
                    if (current.isNotEmpty() && !current.endsWith(" ")) {
                        current.append(' ')
                    }
                    continue
                }
                val candidate = (current.toString() + token).trimEnd()
                if (current.isNotEmpty() && paint.measureText(candidate) > maxWidth) {
                    lines.add(current.toString().trimEnd())
                    current.clear()
                }
                if (paint.measureText(token) > maxWidth) {
                    for (ch in token) {
                        val next = current.toString() + ch
                        if (current.isNotEmpty() && paint.measureText(next) > maxWidth) {
                            lines.add(current.toString())
                            current.clear()
                        }
                        current.append(ch)
                    }
                } else {
                    current.append(token)
                }
            }
            if (current.isNotEmpty()) lines.add(current.toString().trimEnd())
            if (current.isEmpty() && paragraph.isEmpty()) lines.add("")
        }
        return lines.filter { it.isNotEmpty() }
    }

    private fun tokenize(text: String): List<String> {
        val tokens = mutableListOf<String>()
        val word = StringBuilder()
        fun flush() {
            if (word.isNotEmpty()) {
                tokens.add(word.toString())
                word.clear()
            }
        }
        for (ch in text) {
            when {
                ch == ' ' -> {
                    flush()
                    tokens.add(" ")
                }
                isIdeograph(ch) -> {
                    flush()
                    tokens.add(ch.toString())
                }
                isKana(ch) -> word.append(ch)
                ch.code > 0xFF -> {
                    flush()
                    tokens.add(ch.toString())
                }
                else -> word.append(ch)
            }
        }
        flush()
        return tokens
    }

    private fun isIdeograph(ch: Char): Boolean =
        ch.code in 0x3400..0x4DBF || ch.code in 0x4E00..0x9FFF

    private fun isKana(ch: Char): Boolean =
        ch.code in 0x3040..0x30FF

    private fun pack(bitmap: Bitmap): ByteArray {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()
        val bytes = ByteArray(width * height / 8)
        for (row in 0 until height) {
            for (col in 0 until width) {
                if ((pixels[row * width + col] ushr 24) > 96) {
                    val offset = row * (width / 8) + col / 8
                    bytes[offset] =
                        (bytes[offset].toInt() or (0x80 ushr (col % 8))).toByte()
                }
            }
        }
        return bytes
    }
}
