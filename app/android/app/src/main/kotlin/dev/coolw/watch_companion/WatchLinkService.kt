package dev.coolw.watch_companion

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import io.flutter.plugin.common.EventChannel
import java.io.OutputStream
import java.util.Calendar
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * 前台服务持有整条 SPP 链路，Flutter 退出后连接仍然保持。
 *
 * 除透传字节外，服务自己也会：
 *  - 每 2 秒补一次心跳，避免手表 4 秒无帧就判定“无连接”；
 *  - 解析手表 0x10 播放控制，直接操作 Android 当前媒体会话；
 *  - 把系统媒体的标题/艺人渲染成 128×32 点阵（0x0B）下发，
 *    长标题每 3 秒翻页；
 *  - 把系统通知转发到手表消息槽（0x02）。
 */
class WatchLinkService : Service() {
    companion object {
        const val ACTION_CONNECT = "dev.coolw.watch_companion.CONNECT"
        const val ACTION_DISCONNECT = "dev.coolw.watch_companion.DISCONNECT"
        const val EXTRA_ADDRESS = "address"

        private const val CHANNEL_ID = "watch_link"
        private const val NOTIFICATION_ID = 41
        private val SPP_UUID: UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

        private const val PREFS = "watch_link"
        private const val KEY_ADDRESS = "last_address"
        private const val KEY_MEDIA = "media_follow"
        private const val KEY_NOTIFY = "notification_forward"
        private const val KEY_MESSAGES = "cached_messages"

        @Volatile
        var instance: WatchLinkService? = null
            private set

        @Volatile
        private var sink: EventChannel.EventSink? = null

        private val main = Handler(Looper.getMainLooper())

        /** 等 Flutter 侧 connect 的结果；服务可能比 Activity 活得久。 */
        var pendingConnect: ((Boolean, String?) -> Unit)? = null

        val isConnected: Boolean
            get() = instance?.connected?.get() == true

        val cachedMessageCount: Int
            get() = instance?.messageCount() ?: 0

        fun attachSink(value: EventChannel.EventSink?) {
            sink = value
            if (value != null) {
                emitState(instance?.connected?.get() == true, null)
            }
        }

        fun emitState(connected: Boolean, message: String?) {
            val payload = mutableMapOf<String, Any?>(
                "type" to "state",
                "connected" to connected,
            )
            if (message != null) payload["message"] = message
            emit(payload)
        }

        fun emit(payload: Map<String, Any?>) {
            main.post { sink?.success(payload) }
        }
    }

    private lateinit var prefs: SharedPreferences
    private val worker = Executors.newCachedThreadPool()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val writeLock = Any()
    private val stopping = AtomicBoolean(false)
    private val connected = AtomicBoolean(false)
    private val bitmapBusy = AtomicBoolean(false)
    private val bitmapLock = Any()

    @Volatile
    private var socket: BluetoothSocket? = null

    @Volatile
    private var output: OutputStream? = null

    @Volatile
    private var currentAddress: String? = null

    @Volatile
    private var lastTraffic = 0L

    @Volatile
    private var statusText = "未连接"

    private var retryRunnable: Runnable? = null
    private var keepAliveRunnable: Runnable? = null
    private var mediaRunnable: Runnable? = null

    private var mediaFollow = true
    private var notificationForward = true
    private var mediaPages: List<WatchTextRasterizer.Page> = emptyList()
    private var mediaPage = 0
    private var mediaKey = ""
    private var mediaPlaying = false
    private var messageMode = false
    @Volatile
    private var watchMusicActive = false
    private val messages = ArrayDeque<CachedMessage>()

    private val scanner = FrameScanner { cmd, first ->
        when (cmd) {
            0x10 -> {
                MediaBridge.control(first, applicationContext)
                if (watchMusicActive) {
                    mainHandler.postDelayed({
                        MediaBridge.refresh(applicationContext)
                    }, 250)
                }
            }
            0x14 -> onMessageListRequested()
            0x15 -> onMessageDetailRequested(first)
            0x16 -> onMessageLeave()
            0x2E -> onWatchExtFlags(first)
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        mediaFollow = prefs.getBoolean(KEY_MEDIA, true)
        notificationForward = prefs.getBoolean(KEY_NOTIFY, true)
        NotificationRules.load(this)
        loadPersistedMessages()
        createChannel()
        mainHandler.post { WatchNotificationListener.rescan() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_DISCONNECT -> {
                pendingConnect?.invoke(false, null)
                pendingConnect = null
                shutdown()
                return START_NOT_STICKY
            }

            ACTION_CONNECT -> {
                val address = intent.getStringExtra(EXTRA_ADDRESS)
                startForegroundCompat("正在连接手表…")
                if (!address.isNullOrBlank()) {
                    connect(address)
                } else {
                    finishConnect(false, "蓝牙地址为空")
                }
            }

            else -> {
                // 被系统回收后重建：沿用上次地址自动重连。
                startForegroundCompat("等待手表…")
                val address = currentAddress
                    ?: prefs.getString(KEY_ADDRESS, null)
                if (!address.isNullOrBlank()) {
                    connect(address)
                } else {
                    stopSelf()
                }
            }
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    fun setMediaFollow(enabled: Boolean) {
        mediaFollow = enabled
        prefs.edit().putBoolean(KEY_MEDIA, enabled).apply()
        mainHandler.post {
            if (enabled) {
                pushMediaNow(force = true)
                if (watchMusicActive) startMediaSyncPoll()
            } else {
                stopMediaSyncPoll()
                mediaRunnable?.let { mainHandler.removeCallbacks(it) }
                mediaRunnable = null
            }
        }
    }

    fun setNotificationForward(enabled: Boolean) {
        notificationForward = enabled
        prefs.edit().putBoolean(KEY_NOTIFY, enabled).apply()
    }

    /** Flutter 侧发送原始 A5 帧。 */
    fun send(bytes: ByteArray, done: (Boolean) -> Unit) {
        worker.execute {
            // 抓屏会把整块 framebuffer 按裸流回传；无论抓屏请求还是普通命令，
            // 都先避让正在进行的点阵下发或 0x11 图像回传，避免 write() 因 imageGuard 直接返回 false。
            val deadline = SystemClock.uptimeMillis() + 10_000
            while (((isSnapshotRequest(bytes) && bitmapBusy.get()) ||
                    scanner.imageGuardActive()) &&
                SystemClock.uptimeMillis() < deadline
            ) {
                Thread.sleep(15)
            }
            val ok = writeOrReport(bytes)
            mainHandler.post { done(ok) }
        }
    }

    private fun isSnapshotRequest(bytes: ByteArray): Boolean =
        bytes.size == 5 &&
            bytes[0] == 0xA5.toByte() &&
            bytes[1] == 0x06.toByte() &&
            bytes[2] == 0.toByte()

    /** 主动断开并停掉前台服务（用户在 App 里点“断开设备”）。 */
    fun shutdown() {
        stopping.set(true)
        emitState(false, null)
        stopLinkInternal()
        stopForegroundCompat()
        stopSelf()
    }

    override fun onDestroy() {
        instance = null
        pendingConnect?.invoke(false, "服务已停止")
        pendingConnect = null
        stopping.set(true)
        stopLinkInternal()
        worker.shutdownNow()
        super.onDestroy()
    }

    // ---- 连接 ---------------------------------------------------------------

    private fun connect(address: String) {
        if (connected.get() && address.equals(currentAddress, ignoreCase = true)) {
            // Flutter 重启后直接挂到已有连接上，不再重连一次。
            emitState(true, null)
            updateNotification(statusText)
            finishConnect(true, null)
            messageMode = false
            watchMusicActive = false
            requestWatchPoolState()
            return
        }
        stopping.set(false)
        worker.execute {
            try {
                stopLinkInternal()
                currentAddress = address
                val adapter = BluetoothAdapter.getDefaultAdapter()
                    ?: throw IllegalStateException("设备不支持蓝牙")
                if (!adapter.isEnabled) {
                    throw IllegalStateException("请先打开系统蓝牙")
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                    checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
                    PackageManager.PERMISSION_GRANTED
                ) {
                    throw SecurityException("缺少蓝牙连接权限")
                }
                val device: BluetoothDevice = adapter.getRemoteDevice(address)
                val candidate = connectSocket(device)
                synchronized(writeLock) {
                    socket = candidate
                    output = candidate.outputStream
                }
                connected.set(true)
                lastTraffic = SystemClock.uptimeMillis()
                prefs.edit().putString(KEY_ADDRESS, address).apply()
                startReadLoop(candidate)
                statusText = "已连接手表"
                Log.d("WatchLink", "已连接 $address")
                emitState(true, null)
                updateNotification(statusText)
                finishConnect(true, null)
                startKeepAlive()
                sendTime()
                messageMode = false
                watchMusicActive = false
                requestWatchPoolState()
                mainHandler.post { WatchNotificationListener.rescan() }
            } catch (error: Exception) {
                connected.set(false)
                val message = error.message ?: "连接失败"
                Log.d("WatchLink", "连接失败：$message")
                statusText = "等待手表…"
                emitState(false, message)
                updateNotification(statusText)
                finishConnect(false, message)
                scheduleReconnect()
            }
        }
    }

    /**
     * HC-04 与 PC 端一致优先裸 RFCOMM 通道 1，失败再试标准 SPP UUID。
     */
    private fun connectSocket(device: BluetoothDevice): BluetoothSocket {
        var last: Exception? = null
        try {
            val method = device.javaClass.getMethod(
                "createRfcommSocket",
                Int::class.javaPrimitiveType,
            )
            val channelSocket = method.invoke(device, 1) as BluetoothSocket
            try {
                channelSocket.connect()
                return channelSocket
            } catch (error: Exception) {
                last = error
                try {
                    channelSocket.close()
                } catch (_: Exception) {
                }
            }
        } catch (error: Exception) {
            last = error
        }

        try {
            val uuidSocket = device.createRfcommSocketToServiceRecord(SPP_UUID)
            try {
                uuidSocket.connect()
                return uuidSocket
            } catch (error: Exception) {
                last = error
                try {
                    uuidSocket.close()
                } catch (_: Exception) {
                }
            }
        } catch (error: Exception) {
            last = error
        }

        throw IllegalStateException(
            "SPP 连接失败：请确认 HC-04 已配对、手表上电，且没有被电脑占用" +
                "（${last?.message ?: "未知错误"}）",
        )
    }

    private fun finishConnect(ok: Boolean, message: String?) {
        val callback = pendingConnect
        pendingConnect = null
        mainHandler.post { callback?.invoke(ok, message) }
    }

    private fun startReadLoop(sock: BluetoothSocket) {
        val thread = Thread({
            val buffer = ByteArray(2048)
            try {
                val input = sock.inputStream
                while (connected.get() && socket === sock) {
                    val count = input.read(buffer)
                    if (count <= 0) break
                    val data = buffer.copyOf(count)
                    lastTraffic = SystemClock.uptimeMillis()
                    emit(mapOf("type" to "data", "bytes" to data))
                    scanner.feed(data)
                }
            } catch (_: Exception) {
            } finally {
                if (socket === sock && connected.getAndSet(false)) {
                    Log.d("WatchLink", "链路断开，15 秒后重连")
                    closeSocket()
                    statusText = "等待手表…"
                    emitState(false, "链路已断开")
                    updateNotification(statusText)
                    scheduleReconnect()
                }
            }
        }, "watch-link-read")
        thread.start()
    }

    private fun stopLinkInternal() {
        connected.set(false)
        cancelTimers()
        closeSocket()
        scanner.reset()
    }

    private fun closeSocket() {
        synchronized(writeLock) {
            try {
                output?.close()
            } catch (_: Exception) {
            }
            try {
                socket?.close()
            } catch (_: Exception) {
            }
            output = null
            socket = null
        }
    }

    private fun scheduleReconnect() {
        if (stopping.get()) return
        val address = currentAddress ?: return
        retryRunnable?.let { mainHandler.removeCallbacks(it) }
        val runnable = object : Runnable {
            override fun run() {
                if (stopping.get() || connected.get()) return
                connect(address)
            }
        }
        retryRunnable = runnable
        mainHandler.postDelayed(runnable, 15_000)
    }

    private fun cancelTimers() {
        retryRunnable?.let { mainHandler.removeCallbacks(it) }
        retryRunnable = null
        keepAliveRunnable?.let { mainHandler.removeCallbacks(it) }
        keepAliveRunnable = null
        mediaRunnable?.let { mainHandler.removeCallbacks(it) }
        mediaRunnable = null
        stopMediaSyncPoll()
    }

    // ---- 写入 ---------------------------------------------------------------

    private fun write(bytes: ByteArray): Boolean {
        if (!connected.get()) return false
        if (scanner.imageGuardActive()) return false
        return try {
            synchronized(writeLock) {
                val stream = output ?: return false
                stream.write(bytes)
                stream.flush()
            }
            lastTraffic = SystemClock.uptimeMillis()
            true
        } catch (_: Exception) {
            false
        }
    }

    /** 写失败通常意味着对端已经掉线；主动收尾并进入自动重连。 */
    private fun writeOrReport(bytes: ByteArray): Boolean {
        val ok = write(bytes)
        if (!ok && connected.get() && !scanner.imageGuardActive()) {
            reportLinkLoss()
        }
        return ok
    }

    private fun reportLinkLoss() {
        if (!connected.getAndSet(false)) return
        mainHandler.post {
            closeSocket()
            cancelTimers()
            statusText = "等待手表…"
            emitState(false, "链路已断开")
            updateNotification(statusText)
            scheduleReconnect()
        }
    }

    private fun buildFrame(cmd: Int, payload: ByteArray): ByteArray {
        val frame = ByteArray(payload.size + 4)
        frame[0] = 0xA5.toByte()
        frame[1] = cmd.toByte()
        frame[2] = payload.size.toByte()
        System.arraycopy(payload, 0, frame, 3, payload.size)
        var check = 0
        for (i in 0 until frame.size - 1) {
            check = check xor (frame[i].toInt() and 0xFF)
        }
        frame[frame.size - 1] = check.toByte()
        return frame
    }

    private fun sendTime() {
        val now = Calendar.getInstance()
        val payload = byteArrayOf(
            (now.get(Calendar.YEAR) and 0xFF).toByte(),
            ((now.get(Calendar.YEAR) shr 8) and 0xFF).toByte(),
            (now.get(Calendar.MONTH) + 1).toByte(),
            now.get(Calendar.DAY_OF_MONTH).toByte(),
            now.get(Calendar.HOUR_OF_DAY).toByte(),
            now.get(Calendar.MINUTE).toByte(),
            now.get(Calendar.SECOND).toByte(),
        )
        worker.execute { writeOrReport(buildFrame(0x01, payload)) }
    }

    /**
     * 手表 4 秒收不到合法帧就会显示“无连接”，这里补心跳。
     * 只有 1.5 秒内没有任何收发才发，避免和 App 的命令抢 ack。
     */
    private fun startKeepAlive() {
        keepAliveRunnable?.let { mainHandler.removeCallbacks(it) }
        val runnable = object : Runnable {
            override fun run() {
                if (!connected.get()) return
                val now = SystemClock.uptimeMillis()
                if (!scanner.imageGuardActive() && now - lastTraffic > 1_500) {
                    worker.execute {
                        writeOrReport(buildFrame(0x09, ByteArray(0)))
                    }
                }
                mainHandler.postDelayed(this, 2_000)
            }
        }
        keepAliveRunnable = runnable
        mainHandler.postDelayed(runnable, 2_000)
    }

    // ---- 媒体 ---------------------------------------------------------------

    private fun requestWatchPoolState() {
        worker.execute {
            writeOrReport(buildFrame(0x2D, byteArrayOf(0x00)))
        }
    }

    private var mediaPollRunnable: Runnable? = null
    private var mediaPollGeneration = 0

    private fun startMediaSyncPoll() {
        val generation = ++mediaPollGeneration
        val runnable = object : Runnable {
            override fun run() {
                if (generation != mediaPollGeneration) return
                // 音乐页还开着时，短暂掉线不能把轮询掐死；关掉跟随或离开音乐页才停。
                if (!watchMusicActive || !mediaFollow) return
                if (connected.get()) MediaBridge.refresh(applicationContext)
                mainHandler.postDelayed(this, 2_000)
            }
        }
        mediaPollRunnable?.let { mainHandler.removeCallbacks(it) }
        mediaPollRunnable = runnable
        mainHandler.postDelayed(runnable, 2_000)
    }

    private fun stopMediaSyncPoll() {
        mediaPollGeneration++
        mediaPollRunnable?.let { mainHandler.removeCallbacks(it) }
        mediaPollRunnable = null
    }

    private fun onWatchExtFlags(flags: Int) {
        val active = (flags and 0x10) != 0
        mainHandler.post {
            if (active == watchMusicActive) return@post
            watchMusicActive = active
            if (active) {
                MediaBridge.refresh(applicationContext)
                pushMediaNow(force = true)
                startMediaSyncPoll()
            } else {
                stopMediaSyncPoll()
                mediaRunnable?.let { mainHandler.removeCallbacks(it) }
                mediaRunnable = null
                mediaKey = ""
            }
        }
    }

    fun onMediaChanged(title: String, artist: String, playing: Boolean) {
        mainHandler.post {
            val key = "$title\u0000$artist"
            if (watchMusicActive && !messageMode && mediaFollow && connected.get()) {
                if (key != mediaKey) {
                    mediaKey = key
                    mediaPages = WatchTextRasterizer.pages(title, artist)
                    mediaPage = 0
                    scheduleMediaPush()
                }
                if (playing != mediaPlaying) {
                    mediaPlaying = playing
                    sendPlayState(playing)
                }
            }
            if (title.isNotBlank()) {
                statusText = "已连接 · $title"
                updateNotification(statusText)
            }
        }
    }

    private fun pushMediaNow(force: Boolean) {
        if (!watchMusicActive || messageMode || !mediaFollow || !connected.get()) return
        var snapshot = MediaBridge.current
        if (snapshot.title.isBlank()) {
            MediaBridge.refresh(applicationContext)
            snapshot = MediaBridge.current
        }
        if (snapshot.title.isBlank()) return
        val key = "${snapshot.title}\u0000${snapshot.artist}"
        if (!force && key == mediaKey) return
        mediaKey = key
        mediaPages = WatchTextRasterizer.pages(snapshot.title, snapshot.artist)
        mediaPage = 0
        mediaPlaying = snapshot.playing
        sendPlayState(snapshot.playing)
        scheduleMediaPush()
    }

    private fun scheduleMediaPush() {
        mediaRunnable?.let { mainHandler.removeCallbacks(it) }
        mediaRunnable = null
        if (!watchMusicActive || messageMode || !mediaFollow || mediaPages.isEmpty()) return
        val runnable = object : Runnable {
            override fun run() {
                if (!connected.get() || !mediaFollow || messageMode || !watchMusicActive) return
                if (scanner.imageGuardActive()) {
                    mainHandler.postDelayed(this, 1_000)
                    return
                }
                val page = mediaPages[mediaPage.coerceIn(0, mediaPages.size - 1)]
                worker.execute { sendBitmap(WatchTextRasterizer.render(page)) }
                if (mediaPages.size > 1) {
                    mediaPage = (mediaPage + 1) % mediaPages.size
                    mainHandler.postDelayed(this, 3_000)
                }
            }
        }
        mediaRunnable = runnable
        mainHandler.postDelayed(runnable, 120)
    }

    /** 0x0B：128×32 单色点阵，分 4 块；媒体需要先清掉旧文字歌词。 */
    private fun sendBitmap(bits: ByteArray): Boolean {
        if (bits.size != WatchTextRasterizer.BYTES) return false
        return sendMaskChunks(0x0B, bits, 1, clearLyric = true)
    }

    /**
     * 点阵按 128 字节一块下发；消息列表 0x22（14 块）、详情 0x23（20 块）。
     * 每块先试 RLE（0x26），压缩后没有变小就退回原始分块。
     * 与抓屏互斥，并和媒体点阵共用一把锁，避免两块图交叉。
     */
    private fun sendMaskChunks(
        cmd: Int,
        bits: ByteArray,
        maskType: Int,
        clearLyric: Boolean = false,
    ): Boolean =
        synchronized(bitmapLock) {
            if (bits.isEmpty() || bits.size % 128 != 0) return false
            bitmapBusy.set(true)
            try {
                if (clearLyric) {
                    // 新一帧点阵到达前，手表会回退到 0x03 的文字歌词（小字库容易出方框）。
                    writeOrReport(
                        buildFrame(0x03, "...".toByteArray(Charsets.UTF_8)),
                    )
                }
                val chunks = bits.size / 128
                var rleSaved = 0
                var rawBlocks = 0
                for (chunk in 0 until chunks) {
                    if (!connected.get()) return false
                    waitForCapture()
                    if (!connected.get()) return false

                    val encoded = rleEncode(bits, chunk * 128)
                    // 原始分块是 1+128 字节；RLE 只有更短时才值得用。
                    val frame = if (encoded.size <= 126) {
                        rleSaved += 128 - encoded.size
                        // 2 字节头 + RLE 数据；固件解出 128 字节后走原分块逻辑。
                        val payload = ByteArray(2 + encoded.size)
                        payload[0] = maskType.toByte()
                        payload[1] = chunk.toByte()
                        System.arraycopy(encoded, 0, payload, 2, encoded.size)
                        buildFrame(0x26, payload)
                    } else {
                        rawBlocks++
                        val payload = ByteArray(1 + 128)
                        payload[0] = chunk.toByte()
                        System.arraycopy(bits, chunk * 128, payload, 1, 128)
                        buildFrame(cmd, payload)
                    }

                    // 抓屏刚好开始时先让路，抓完再补这一块，不能半途丢掉。
                    var sent = false
                    var tries = 0
                    while (!sent && tries < 80) {
                        sent = writeOrReport(frame)
                        if (sent) break
                        if (!connected.get()) return false
                        if (!scanner.imageGuardActive()) return false
                        Thread.sleep(50)
                        tries++
                    }
                    if (!sent) return false
                    Thread.sleep(6)
                }
                Log.d(
                    "WatchLink",
                    "点阵 type=$maskType 完成：RLE 省 ${rleSaved}B，原始块 $rawBlocks",
                )
                true
            } finally {
                bitmapBusy.set(false)
            }
        }

    /** 128 字节 1bpp 块的游程编码；最坏 256 字节，调用方按大小决定是否采用。 */
    private fun rleEncode(bits: ByteArray, offset: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream(96)
        var i = 0
        while (i < 128) {
            val value = bits[offset + i]
            var run = 1
            while (i + run < 128 &&
                bits[offset + i + run] == value &&
                run < 255
            ) {
                run++
            }
            out.write(value.toInt() and 0xFF)
            out.write(run)
            i += run
        }
        return out.toByteArray()
    }

    private fun waitForCapture() {
        val deadline = SystemClock.uptimeMillis() + 8_000
        while (scanner.imageGuardActive() &&
            SystemClock.uptimeMillis() < deadline
        ) {
            Thread.sleep(50)
        }
    }

    private fun sendPlayState(playing: Boolean) {
        worker.execute {
            writeOrReport(buildFrame(0x04, byteArrayOf(if (playing) 1 else 0)))
        }
    }

    // ---- 通知缓存与消息页 ---------------------------------------------------

    private fun loadPersistedMessages() {
        val raw = prefs.getString(KEY_MESSAGES, null) ?: return
        try {
            val arr = org.json.JSONArray(raw)
            messages.clear()
            for (i in 0 until arr.length()) {
                val obj = arr.optJSONObject(i) ?: continue
                messages.addLast(
                    CachedMessage(
                        key = obj.optString("key"),
                        pkg = obj.optString("pkg"),
                        item = WatchMessageRasterizer.Item(
                            app = obj.optString("app"),
                            title = obj.optString("title"),
                            text = obj.optString("text"),
                        ),
                    ),
                )
            }
            while (messages.size > NotificationRules.maxMessages) {
                messages.removeLast()
            }
        } catch (_: Exception) {
        }
    }

    private fun persistMessages() {
        val arr = org.json.JSONArray()
        for (entry in messages) {
            val obj = org.json.JSONObject()
            obj.put("key", entry.key)
            obj.put("pkg", entry.pkg)
            obj.put("app", entry.item.app)
            obj.put("title", entry.item.title)
            obj.put("text", entry.item.text)
            arr.put(obj)
        }
        prefs.edit().putString(KEY_MESSAGES, arr.toString()).apply()
    }

    fun clearNotifications() {
        mainHandler.post {
            messages.clear()
            persistMessages()
            if (connected.get()) {
                worker.execute {
                    synchronized(bitmapLock) {
                        writeOrReport(buildFrame(0x24, ByteArray(0)))
                        writeOrReport(buildFrame(0x2D, byteArrayOf(0x00)))
                    }
                }
            }
        }
    }

    /** 推送一条置顶消息到手表片外 AT24C256 动态 ROM（0x2D prepend + 行点阵 + 槽位 0 详情）。 */
    private fun pushPrependToWatch(count: Int, item: WatchMessageRasterizer.Item) {
        worker.execute {
            if (!connected.get()) return@execute
            waitForCapture()
            if (!connected.get()) return@execute
            val rowBits = WatchMessageRasterizer.row(item)
            val detailBits = WatchMessageRasterizer.detail(item)
            val ok = synchronized(bitmapLock) {
                if (!writeOrReport(buildFrame(0x2D, byteArrayOf(0x01, count.coerceIn(1, 20).toByte())))) {
                    return@synchronized false
                }
                Thread.sleep(6)
                val chunks = (rowBits.size + 127) / 128
                for (sub in 0 until chunks) {
                    val start = sub * 128
                    val len = minOf(128, rowBits.size - start)
                    val payload = ByteArray(2 + len)
                    payload[0] = 0x02
                    payload[1] = sub.toByte()
                    System.arraycopy(rowBits, start, payload, 2, len)
                    if (!writeOrReport(buildFrame(0x2D, payload))) return@synchronized false
                    Thread.sleep(6)
                }
                true
            }
            if (ok) {
                sendDetailSlot(0, detailBits)
                synchronized(bitmapLock) {
                    writeOrReport(buildFrame(0x2D, byteArrayOf(0x00)))
                }
            }
        }
    }

    private fun sendDetailSlot(slot: Int, bits: ByteArray): Boolean =
        synchronized(bitmapLock) {
            if (bits.isEmpty() || bits.size % 128 != 0 || slot !in 0..19) return false
            bitmapBusy.set(true)
            try {
                val chunks = bits.size / 128
                for (chunk in 0 until chunks) {
                    if (!connected.get()) return false
                    waitForCapture()
                    if (!connected.get()) return false
                    val encoded = rleEncode(bits, chunk * 128)
                    val frame = if (encoded.size <= 125) {
                        val payload = ByteArray(3 + encoded.size)
                        payload[0] = 0x05
                        payload[1] = slot.toByte()
                        payload[2] = chunk.toByte()
                        System.arraycopy(encoded, 0, payload, 3, encoded.size)
                        buildFrame(0x26, payload)
                    } else {
                        val payload = ByteArray(2 + 128)
                        payload[0] = slot.toByte()
                        payload[1] = chunk.toByte()
                        System.arraycopy(bits, chunk * 128, payload, 2, 128)
                        buildFrame(0x23, payload)
                    }
                    var sent = false
                    var tries = 0
                    while (!sent && tries < 80) {
                        sent = writeOrReport(frame)
                        if (sent) break
                        if (!connected.get() || !scanner.imageGuardActive()) return false
                        Thread.sleep(50)
                        tries++
                    }
                    if (!sent) return false
                    Thread.sleep(6)
                }
                true
            } finally {
                bitmapBusy.set(false)
            }
        }

    data class IncomingMessage(
        val app: String,
        val title: String,
        val text: String,
        val key: String,
        val pkg: String,
    )

    /** 用通知栏当前内容替换缓存，并整表推给手表。 */
    fun syncMessages(incoming: List<IncomingMessage>) {
        mainHandler.post {
            messages.clear()
            incoming.forEach { item ->
                messages.addLast(
                    CachedMessage(
                        key = item.key,
                        pkg = item.pkg,
                        item = WatchMessageRasterizer.Item(item.app, item.title, item.text),
                    ),
                )
            }
            trimMessages()
            persistMessages()
            Log.i("WatchLink", "同步消息 ${messages.size} 条，连接=${connected.get()}")
            if (connected.get()) pushFullList()
        }
    }

    private fun pushFullList() {
        val items = messages.map { it.item }
        worker.execute {
            synchronized(bitmapLock) {
                writeOrReport(buildFrame(0x25, byteArrayOf(items.size.coerceIn(0, 20).toByte())))
            }
            if (items.isEmpty()) return@execute
            if (sendMaskChunks(0x22, WatchMessageRasterizer.list(items), 2)) {
                sendDetailSlot(0, WatchMessageRasterizer.detail(items.first()))
                synchronized(bitmapLock) {
                    writeOrReport(buildFrame(0x2D, byteArrayOf(0x00)))
                }
            }
        }
    }

    /** 通知入本机持久化缓存，已连接时同步增量写入手表 AT24C256 动态 ROM。 */
    fun cacheNotification(
        app: String,
        title: String,
        text: String,
        key: String,
        pkg: String,
    ) {
        if (!notificationForward) return
        mainHandler.post {
            val hadSame = messages.any {
                it.key == key || (NotificationRules.merge && it.pkg == pkg)
            }
            messages.removeAll { it.key == key }
            if (NotificationRules.merge) {
                // 同一应用只留最新一条，避免占满全部位置。
                messages.removeAll { it.pkg == pkg }
            }
            val item = WatchMessageRasterizer.Item(app, title, text)
            messages.addFirst(CachedMessage(key, pkg, item))
            trimMessages()
            persistMessages()
            if (connected.get()) {
                if (!hadSame) {
                    pushPrependToWatch(messages.size, item)
                } else {
                    val items = messages.map { it.item }
                    worker.execute {
                        synchronized(bitmapLock) {
                            writeOrReport(buildFrame(0x25, byteArrayOf(items.size.toByte())))
                        }
                        if (sendMaskChunks(0x22, WatchMessageRasterizer.list(items), 2)) {
                            sendDetailSlot(0, WatchMessageRasterizer.detail(item))
                            synchronized(bitmapLock) {
                                writeOrReport(buildFrame(0x2D, byteArrayOf(0x00)))
                            }
                        }
                    }
                }
            }
        }
    }

    fun removeNotification(key: String) {
        mainHandler.post {
            if (messages.removeAll { it.key == key }) {
                persistMessages()
            }
        }
    }

    /** 规则修改后立即把缓存收紧到新的条数上限。 */
    fun applyMessageRules() {
        mainHandler.post {
            trimMessages()
            persistMessages()
        }
    }

    private fun trimMessages() {
        while (messages.size > NotificationRules.maxMessages) {
            messages.removeLast()
        }
    }

    fun messageCount(): Int = messages.size

    /** 手表打开一级消息页：若本机有缓存则同步点阵，无缓存时保留手表 AT24C256 持久化历史。 */
    private fun onMessageListRequested() {
        mainHandler.post {
            messageMode = true
            mediaRunnable?.let { mainHandler.removeCallbacks(it) }
            mediaRunnable = null
            if (messages.isEmpty()) {
                Log.i("WatchLink", "手表要消息列表，缓存是空的，重新扫通知栏")
                WatchNotificationListener.rescan()
                return@post
            }
            Log.i("WatchLink", "手表要消息列表：${messages.size} 条")
            pushFullList()
        }
    }

    /** 选中某条消息：本机存在缓存时刷新 128×160 详情点阵；否则沿用手表本地动态 ROM。 */
    private fun onMessageDetailRequested(index: Int) {
        mainHandler.post {
            messageMode = true
            val item = messages.getOrNull(index)?.item ?: return@post
            worker.execute {
                sendDetailSlot(index, WatchMessageRasterizer.detail(item))
            }
        }
    }

    private fun onMessageLeave() {
        mainHandler.post {
            messageMode = false
            // 不在这里推媒体，避免把用户从主页强行拉到音乐页；
            // 下一次媒体状态变化会正常恢复标题。
        }
    }

    // ---- 前台通知 -----------------------------------------------------------

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val manager = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            CHANNEL_ID,
            "手表连接",
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "保持与 AT32 手表的蓝牙连接"
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    private fun buildNotification(status: String): Notification {
        val open = PendingIntent.getActivity(
            this,
            1,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or immutableFlag(),
        )
        val disconnect = PendingIntent.getService(
            this,
            2,
            Intent(this, WatchLinkService::class.java)
                .setAction(ACTION_DISCONNECT),
            PendingIntent.FLAG_UPDATE_CURRENT or immutableFlag(),
        )
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setContentTitle("AT32 手表")
            .setContentText(status)
            .setSmallIcon(R.drawable.ic_stat_watch)
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .addAction(
                Notification.Action.Builder(
                    null,
                    "断开",
                    disconnect,
                ).build(),
            )
            .build()
    }

    private fun startForegroundCompat(status: String) {
        statusText = status
        val notification = buildNotification(status)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
    }

    private fun updateNotification(status: String) {
        val manager = getSystemService(NotificationManager::class.java)
        try {
            manager.notify(NOTIFICATION_ID, buildNotification(status))
        } catch (_: Exception) {
        }
    }

    private fun immutableFlag(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PendingIntent.FLAG_IMMUTABLE
        } else {
            0
        }
}

private data class CachedMessage(
    val key: String,
    val pkg: String,
    val item: WatchMessageRasterizer.Item,
)

/**
 * 只挑控制/消息请求帧；抓屏（0x11）是 40KB 裸流，必须整帧跳过，
 * 否则里面的随机字节会被误判成控制命令。
 */
private class FrameScanner(private val onFrame: (Int, Int) -> Unit) {
    private var buffer = ByteArray(1024)
    private var size = 0

    @Volatile
    private var imageUntil = 0L

    fun imageGuardActive(): Boolean =
        SystemClock.uptimeMillis() < imageUntil

    fun reset() {
        size = 0
        imageUntil = 0L
    }

    fun feed(data: ByteArray) {
        if (size + data.size > MAX_BUFFER) reset()
        ensure(size + data.size)
        System.arraycopy(data, 0, buffer, size, data.size)
        size += data.size
        drain()
    }

    private fun ensure(capacity: Int) {
        if (capacity <= buffer.size) return
        var next = buffer.size
        while (next < capacity) next *= 2
        buffer = buffer.copyOf(minOf(next, MAX_BUFFER))
    }

    private fun drain() {
        var index = 0
        while (true) {
            var mark = -1
            for (i in index until size) {
                if (buffer[i] == 0xA5.toByte()) {
                    mark = i
                    break
                }
            }
            if (mark < 0) {
                index = size
                break
            }
            index = mark
            val remaining = size - index
            if (remaining < 3) break

            val cmd = buffer[index + 1].toInt() and 0xFF
            val lengthByte = buffer[index + 2].toInt() and 0xFF
            val header: Int
            val payload: Int
            if (cmd == 0x11 && lengthByte == 0xFF) {
                if (remaining < 6) {
                    imageUntil = SystemClock.uptimeMillis() + 8_000
                    break
                }
                payload =
                    (buffer[index + 3].toInt() and 0xFF) or
                        ((buffer[index + 4].toInt() and 0xFF) shl 8) or
                        ((buffer[index + 5].toInt() and 0xFF) shl 16)
                header = 6
            } else {
                payload = lengthByte
                header = 3
            }

            val total = header + payload + 1
            if (payload < 0 || total > MAX_BUFFER) {
                index += 1
                continue
            }
            if (remaining < total) {
                if (cmd == 0x11 && lengthByte == 0xFF) {
                    imageUntil = SystemClock.uptimeMillis() + 8_000
                }
                break
            }

            var check = 0
            for (i in index until index + total - 1) {
                check = check xor (buffer[i].toInt() and 0xFF)
            }
            val ok = check == (buffer[index + total - 1].toInt() and 0xFF)
            if (ok) {
                if (cmd == 0x10 || cmd == 0x14 || cmd == 0x15 || cmd == 0x16) {
                    val first = if (payload >= 1) {
                        buffer[index + header].toInt() and 0xFF
                    } else {
                        0
                    }
                    onFrame(cmd, first)
                } else if (cmd == 0x2E && payload >= 13) {
                    val flags = buffer[index + header + 12].toInt() and 0xFF
                    onFrame(cmd, flags)
                }
                if (cmd == 0x11) {
                    imageUntil = SystemClock.uptimeMillis() + 800
                }
                index += total
            } else {
                index += 1
            }
        }

        if (index > 0) {
            System.arraycopy(buffer, index, buffer, 0, size - index)
            size -= index
        }
    }

    companion object {
        private const val MAX_BUFFER = 65_536
    }
}
