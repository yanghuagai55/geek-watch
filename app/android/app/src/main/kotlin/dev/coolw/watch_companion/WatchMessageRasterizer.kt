package dev.coolw.watch_companion

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface

/**
 * 消息页点阵：
 *  - 列表 128×112（5 行 × 22px），块 0..13；
 *  - 详情 128×160（整屏），块 0..19。
 * 由手机排版，绕开手表 6×12 小字库缺字出方框的问题。
 */
object WatchMessageRasterizer {
    const val LIST_W = 128
    const val LIST_H = 112
    const val LIST_CHUNKS = LIST_W * LIST_H / 8 / 128

    const val DETAIL_W = 128
    const val DETAIL_H = 160
    const val DETAIL_CHUNKS = DETAIL_W * DETAIL_H / 8 / 128

    data class Item(val app: String, val title: String, val text: String)

    private const val ROW_H = 22f
    private const val MARGIN = 4f

    private val namePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 12f
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val detailPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        typeface = Typeface.SANS_SERIF
    }

    fun list(items: List<Item>): ByteArray {
        val picked = items.take(20)
        val rawBytes = maxOf(1, picked.size) * 22 * (LIST_W / 8)
        val chunks = (rawBytes + 127) / 128
        val height = chunks * 8
        val bitmap = Bitmap.createBitmap(LIST_W, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)
        val maxWidth = LIST_W - MARGIN * 2

        picked.forEachIndexed { index, item ->
            drawRowText(canvas, index * ROW_H, item, maxWidth)
        }
        return pack(bitmap)
    }

    /** 渲染单条消息列表行 128×22（352 字节），用于 0x2D 单条增量插入动态 ROM。 */
    fun row(item: Item): ByteArray {
        val bitmap = Bitmap.createBitmap(LIST_W, ROW_H.toInt(), Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)
        drawRowText(canvas, 0f, item, LIST_W - MARGIN * 2)
        return pack(bitmap)
    }

    private fun drawRowText(canvas: Canvas, top: Float, item: Item, maxWidth: Float) {
        // 一级列表只显示应用名；读不到应用名时退回消息前几个字。
        val heading = item.app.ifBlank { preview(item).take(6) }
        if (heading.isEmpty()) return
        canvas.drawText(
            ellipsize(heading, namePaint, maxWidth),
            MARGIN,
            top + 15.5f,
            namePaint,
        )
    }

    fun detail(item: Item?): ByteArray {
        val bitmap = Bitmap.createBitmap(DETAIL_W, DETAIL_H, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)

        val text = detailText(item)
        if (text.isEmpty()) {
            drawEmpty(canvas)
            return pack(bitmap)
        }

        val maxWidth = DETAIL_W - MARGIN * 2
        var chosenSize = 9f
        var chosenLines: List<String> = emptyList()
        var size = 16f
        while (size >= 9f) {
            detailPaint.textSize = size
            val lines = wrap(text, detailPaint, maxWidth)
            val lineHeight = size + 2f
            val maxLines = ((DETAIL_H - 6f) / lineHeight).toInt()
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
        val lineHeight = chosenSize + 2f
        val maxLines = ((DETAIL_H - 6f) / lineHeight).toInt()
        val lines = if (chosenLines.size > maxLines) {
            chosenLines.take(maxLines).toMutableList().also { kept ->
                val last = kept.lastOrNull().orEmpty()
                kept[kept.size - 1] = ellipsize(last + "…", detailPaint, maxWidth)
            }
        } else {
            chosenLines
        }
        val metrics = detailPaint.fontMetrics
        var baseline = 3f - metrics.ascent
        for (line in lines) {
            canvas.drawText(line, MARGIN, baseline, detailPaint)
            baseline += lineHeight
        }
        return pack(bitmap)
    }

    private fun detailText(item: Item?): String {
        if (item == null) return ""
        val title = item.title.trim()
        val body = item.text.trim()
        return when {
            title.isEmpty() -> body
            body.isEmpty() -> title
            title == body -> title
            else -> "$title\n$body"
        }.replace("\r", "")
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
                current.append(token)
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
