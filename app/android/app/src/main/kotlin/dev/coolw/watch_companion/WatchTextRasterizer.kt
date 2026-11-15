package dev.coolw.watch_companion

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface

/**
 * 把媒体标题/艺人渲染成固件要的 128×32 单色点阵（512 字节，MSB 在前）。
 * 在原生层渲染是为了让 App 退到后台、Flutter 引擎不跑时也能继续推送。
 *
 * 字号不固定：单行会尽量放大，两行则按实际字形高度铺满 32px，
 * 避免固定字号导致上下被裁、左右留白过多。
 */
object WatchTextRasterizer {
    const val WIDTH = 128
    const val HEIGHT = 32
    const val BYTES = WIDTH * HEIGHT / 8

    data class Page(val lines: List<String>, val textSize: Float)

    private val sizes = floatArrayOf(
        24f, 22f, 20f, 18f, 17f, 16f, 15f, 14f, 13f, 12f, 11f, 10f,
    )

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    /** 标题一行、歌手一行；标题自己就占满两行时只显示标题。 */
    fun pages(title: String, artist: String): List<Page> {
        val heading = title.trim()
        val sub = artist.trim()
        if (heading.isEmpty() && sub.isEmpty()) return emptyList()

        for (size in sizes) {
            val pages = layout(heading, sub, size)
            if (pages.isNotEmpty() && pages.all { fits(it) }) {
                return pages
            }
        }
        return layout(heading, sub, sizes.last())
    }

    fun render(page: Page): ByteArray {
        paint.textSize = page.textSize
        val bitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.TRANSPARENT)

        val visible = page.lines.take(2)
        val bounds = visible.map { line ->
            Rect().also { paint.getTextBounds(line, 0, line.length, it) }
        }
        val gap = gapFor(page.textSize)
        val total = bounds.sumOf { it.height() } +
            (bounds.size - 1).coerceAtLeast(0) * gap
        var top = (HEIGHT - total) / 2f
        for (index in visible.indices) {
            val rect = bounds[index]
            // 用字形墨迹定位：rect.top 是相对基线的负值。
            canvas.drawText(visible[index], WIDTH / 2f, top - rect.top, paint)
            top += rect.height() + gap
        }

        val pixels = IntArray(WIDTH * HEIGHT)
        bitmap.getPixels(pixels, 0, WIDTH, 0, 0, WIDTH, HEIGHT)
        bitmap.recycle()

        val bits = ByteArray(BYTES)
        for (row in 0 until HEIGHT) {
            for (col in 0 until WIDTH) {
                if ((pixels[row * WIDTH + col] ushr 24) > 96) {
                    val offset = row * (WIDTH / 8) + col / 8
                    bits[offset] =
                        (bits[offset].toInt() or (0x80 ushr (col % 8))).toByte()
                }
            }
        }
        return bits
    }

    private fun layout(title: String, artist: String, size: Float): List<Page> {
        paint.textSize = size
        val titleLines = if (title.isNotEmpty()) {
            wrap(title, WIDTH - 2f)
        } else {
            emptyList()
        }
        val lines = if (titleLines.size >= 2 || artist.isEmpty()) {
            titleLines
        } else {
            titleLines + wrap(artist, WIDTH - 2f)
        }
        if (lines.isEmpty()) return emptyList()
        return lines.chunked(2).map { Page(it, size) }
    }

    private fun fits(page: Page): Boolean {
        paint.textSize = page.textSize
        val gap = gapFor(page.textSize)
        var total = (page.lines.size - 1).coerceAtLeast(0) * gap
        for (line in page.lines) {
            val rect = Rect()
            paint.getTextBounds(line, 0, line.length, rect)
            if (rect.width() > WIDTH) return false
            total += rect.height()
        }
        return total <= HEIGHT
    }

    private fun gapFor(size: Float): Float = if (size >= 18f) 2f else 1f

    private fun wrap(text: String, maxWidth: Float): List<String> {
        val lines = mutableListOf<String>()
        val current = StringBuilder()
        for (token in tokenize(text)) {
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
        return lines.filter { it.isNotEmpty() }
    }

    /** 中文逐字、日文假名与英文按词切，避免把单词拆成两页。 */
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
}
