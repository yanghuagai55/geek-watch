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
        private const val MANUAL_PKG = "dev.coolw.manual"
        private val offlineStoreLock = Any()

        @Volatile
        private var offlineCountCache: Int = -1

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
            if (value == null) {
                instance?.setDartTransferActive(false)
            } else {
                emitState(instance?.connected?.get() == true, null)
                emitBusy(instance?.bitmapBusy?.get() == true)
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

        fun emitBusy(busy: Boolean) {
            emit(mapOf("type" to "busy", "busy" to busy))
        }

        fun emit(payload: Map<String, Any?>) {
            main.post { sink?.success(payload) }
        }

        private fun loadOfflineList(context: Context): MutableList<CachedMessage> {
            val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val raw = prefs.getString(KEY_MESSAGES, null)
            if (raw == null) {
                offlineCountCache = 0
                return mutableListOf()
            }
            val out = mutableListOf<CachedMessage>()
            try {
                val arr = org.json.JSONArray(raw)
                for (i in 0 until arr.length()) {
                    val obj = arr.optJSONObject(i) ?: continue
                    out.add(
                        CachedMessage(
                            key = obj.optString("key"),
                            pkg = obj.optString("pkg"),
                            item = WatchMessageRasterizer.Item(
                                app = obj.optString("app"),
                                title = obj.optString("title"),
                                text = obj.optString("text"),
                                categoryId = obj.optString("catId"),
                                categoryLabel = obj.optString("catLabel"),
                            ),
                            isGroupSummary = obj.optBoolean("summary", false),
                            groupKey = obj.optString("groupKey").takeIf { it.isNotEmpty() },
                        ),
                    )
                }
            } catch (_: Exception) {
            }
            offlineCountCache = out.size
            return out
        }

        private fun saveOfflineList(context: Context, list: List<CachedMessage>) {
            offlineCountCache = list.size
            val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val arr = org.json.JSONArray()
            for (entry in list) {
                val obj = org.json.JSONObject()
                obj.put("key", entry.key)
                obj.put("pkg", entry.pkg)
                obj.put("app", entry.item.app)
                obj.put("title", entry.item.title)
                obj.put("text", entry.item.text)
                if (entry.item.categoryId.isNotEmpty()) obj.put("catId", entry.item.categoryId)
                if (entry.item.categoryLabel.isNotEmpty()) obj.put("catLabel", entry.item.categoryLabel)
                if (entry.isGroupSummary) obj.put("summary", true)
                if (!entry.groupKey.isNullOrEmpty()) obj.put("groupKey", entry.groupKey)
                arr.put(obj)
            }
            prefs.edit().putString(KEY_MESSAGES, arr.toString()).apply()
        }

        private fun mergeKey(pkg: String, categoryId: String): String =
            if (categoryId.isNotEmpty()) "$pkg\u0000$categoryId" else pkg

        /** 前台服务未启动时把通知写入持久化缓存，连接手表后立即补推。 */
        fun cacheOfflineNotification(
            context: Context,
            app: String,
            title: String,
            text: String,
            key: String,
            pkg: String,
            categoryId: String = "",
            categoryLabel: String = "",
            isGroupSummary: Boolean = false,
            groupKey: String? = null,
        ): Boolean {
            if (!NotificationRules.isNotificationForwardEnabled(context)) return false
            NotificationRules.load(context)
            val targetMerge = mergeKey(pkg, categoryId)
            synchronized(offlineStoreLock) {
                val list = loadOfflineList(context)
                val unchanged = list.any {
                    it.key == key && it.item.app == app &&
                        it.item.title == title && it.item.text == text &&
                        it.item.categoryId == categoryId
                }
                if (unchanged) return false
                if (!isGroupSummary && !groupKey.isNullOrEmpty()) {
                    list.removeAll { it.isGroupSummary && it.pkg == pkg && it.groupKey == groupKey }
                }
                list.removeAll { it.key == key }
                if (NotificationRules.merge) {
                    list.removeAll { mergeKey(it.pkg, it.item.categoryId) == targetMerge }
                }
                list.add(
                    0,
                    CachedMessage(
                        key = key,
                        pkg = pkg,
                        item = WatchMessageRasterizer.Item(
                            app = app,
                            title = title,
                            text = text,
                            categoryId = categoryId,
                            categoryLabel = categoryLabel,
                        ),
                        isGroupSummary = isGroupSummary,
                        groupKey = groupKey,
                    ),
                )
                while (list.size > NotificationRules.maxMessages) {
                    list.removeAt(list.size - 1)
                }
                saveOfflineList(context, list)
            }
            return true
        }

        fun syncOfflineMessages(context: Context, incoming: List<IncomingMessage>) {
            if (!NotificationRules.isNotificationForwardEnabled(context)) return
            NotificationRules.load(context)
            synchronized(offlineStoreLock) {
                if (incoming.isEmpty()) return
                val existing = loadOfflineList(context)
                val manual = existing.filter { it.pkg == MANUAL_PKG }
                val next = mutableListOf<CachedMessage>()
                next.addAll(manual)
                val seenMergeKeys = HashSet<String>()
                for (item in incoming) {
                    val mk = mergeKey(item.pkg, item.categoryId)
                    if (NotificationRules.merge && !seenMergeKeys.add(mk)) continue
                    next.add(
                        CachedMessage(
                            key = item.key,
                            pkg = item.pkg,
                            item = WatchMessageRasterizer.Item(
                                app = item.app,
                                title = item.title,
                                text = item.text,
                                categoryId = item.categoryId,
                                categoryLabel = item.categoryLabel,
                            ),
                            isGroupSummary = item.isGroupSummary,
                            groupKey = item.groupKey,
                        ),
                    )
                }
                while (next.size > NotificationRules.maxMessages) {
                    next.removeAt(next.size - 1)
                }
                saveOfflineList(context, next)
            }
        }

        fun cachedMessageCount(context: Context): Int {
            instance?.let { return it.messageCount() }
            val cached = offlineCountCache
            if (cached >= 0) return cached
            return synchronized(offlineStoreLock) {
                if (offlineCountCache >= 0) offlineCountCache else loadOfflineList(context).size
            }
        }

        fun setMediaFollow(context: Context, enabled: Boolean) {
            val svc = instance
            if (svc != null) {
                svc.setMediaFollow(enabled)
                return
            }
            context.applicationContext
                .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_MEDIA, enabled)
                .apply()
        }

        fun setNotificationForward(context: Context, enabled: Boolean) {
            val svc = instance
            if (svc != null) {
                svc.setNotificationForward(enabled)
                return
            }
            val appCtx = context.applicationContext
            appCtx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_NOTIFY, enabled)
                .apply()
            if (enabled) {
                main.post {
                    WatchNotificationListener.ensureBound(appCtx)
                    WatchNotificationListener.rescan()
                }
            }
        }

        fun clearMessages(context: Context) {
            val svc = instance
            if (svc != null) {
                svc.clearNotifications()
                return
            }
            val snapshots = WatchNotificationListener.captureActiveSnapshots()
            if (snapshots.isNotEmpty()) {
                NotificationRules.markDismissedBatch(context, snapshots)
            }
            synchronized(offlineStoreLock) {
                saveOfflineList(context, emptyList())
            }
        }

        fun removeOfflineNotification(context: Context, key: String) {
            synchronized(offlineStoreLock) {
                val list = loadOfflineList(context)
                if (list.removeAll { it.key == key }) {
                    saveOfflineList(context, list)
                }
            }
        }
    }

    private lateinit var prefs: SharedPreferences
    private val connectExecutor = Executors.newSingleThreadExecutor()
    private val sendExecutor = Executors.newSingleThreadExecutor()
    private val txExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val writeLock = Any()
    private val messagesLock = Any()
    private val txCoordLock = Any()
    private val dartTransferLock = Object()
    private val stopping = AtomicBoolean(false)
    private val connected = AtomicBoolean(false)
    private val bitmapBusy = AtomicBoolean(false)
    private var bitmapDepth = 0
    private val ackLock = Object()

    @Volatile
    private var ackSeq = 0L

    @Volatile
    private var ackResult = false

    @Volatile
    private var dartTransferActive = false

    @Volatile
    private var dartTransferUntil = 0L

    @Volatile
    private var dartWriteInProgress = false

    @Volatile
    private var dartAckExpectedSeq = -1L

    @Volatile
    private var dartAckPendingUntil = 0L

    @Volatile
    private var pushListGen = 0L

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
    @Volatile
    private var notificationForward = true
    private var mediaPages: List<WatchTextRasterizer.Page> = emptyList()
    private var mediaPage = 0
    private var mediaKey = ""
    private var mediaPlaying = false
    private var messageMode = false
    @Volatile
    private var watchMusicActive = false
    @Volatile
    private var lastWatchMsgCount = -1
    @Volatile
    private var phoneClearGuardUntil = 0L
    @Volatile
    private var lastPushedSignature = ""
    @Volatile
    private var urgentDetailSlot = -1
    private var lastListenerCheckMs = 0L
    private val messages = ArrayDeque<CachedMessage>()

    private val scanner = FrameScanner(
        onFrame = { cmd, first, second ->
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
                0x2E -> onWatchExtPoolState(flags = first, msgCount = second)
            }
        },
        onAck = { ok ->
            synchronized(ackLock) {
                ackResult = ok
                ackSeq++
                dartAckPendingUntil = 0L
                ackLock.notifyAll()
            }
        },
    )

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        mediaFollow = prefs.getBoolean(KEY_MEDIA, true)
        notificationForward = prefs.getBoolean(KEY_NOTIFY, true)
        NotificationRules.load(this)
        loadPersistedMessages()
        createChannel()
        WatchNotificationListener.ensureBound(applicationContext)
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
                try {
                    startForegroundCompat("正在连接手表…")
                } catch (error: SecurityException) {
                    Log.w("WatchLink", "前台服务启动受限：${error.message}")
                    finishConnect(false, "缺少蓝牙权限，无法启动后台连接")
                    stopSelf()
                    return START_NOT_STICKY
                }
                if (!address.isNullOrBlank()) {
                    connect(address)
                } else {
                    finishConnect(false, "蓝牙地址为空")
                }
            }

            else -> {
                // 被系统回收后重建：沿用上次地址自动重连。
                try {
                    startForegroundCompat("等待手表…")
                } catch (error: SecurityException) {
                    Log.w("WatchLink", "重启前台服务受限：${error.message}")
                    stopSelf()
                    return START_NOT_STICKY
                }
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

    fun setDartTransferActive(active: Boolean) {
        synchronized(dartTransferLock) {
            dartTransferActive = active
            dartTransferUntil = if (active) SystemClock.uptimeMillis() + 15_000L else 0L
            dartTransferLock.notifyAll()
        }
    }

    private fun isDartTransferValid(): Boolean =
        dartTransferActive && SystemClock.uptimeMillis() < dartTransferUntil

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
        if (enabled) {
            mainHandler.post {
                WatchNotificationListener.ensureBound(applicationContext)
                WatchNotificationListener.rescan()
            }
        }
    }

    /** Flutter 侧发送原始 A5 帧。 */
    fun send(bytes: ByteArray, done: (Boolean) -> Unit) {
        val cmd = if (bytes.size >= 2 && bytes[0] == 0xA5.toByte()) {
            bytes[1].toInt() and 0xFF
        } else {
            -1
        }
        // 周期轮询（0x09 性能、0x13 传感器）在原生推送或抓屏忙时直接跳过，避免积压爆掉手表 256B 串口环。
        if ((cmd == 0x09 || cmd == 0x13) &&
            (bitmapBusy.get() || scanner.imageGuardActive())
        ) {
            mainHandler.post { done(false) }
            return
        }
        sendExecutor.execute {
            val deadline = SystemClock.uptimeMillis() + 10_000
            while (connected.get() && SystemClock.uptimeMillis() < deadline) {
                val busy = bitmapBusy.get() ||
                    (cmd != 0x06 && scanner.imageGuardActive())
                if (!busy) {
                    if (cmd == 0x06 && scanner.imageGuardActive()) {
                        // 等待上一帧抓屏结束
                    } else {
                        var acquired = false
                        synchronized(txCoordLock) {
                            val stillBusy = bitmapBusy.get() || scanner.imageGuardActive()
                            if (!stillBusy) {
                                dartWriteInProgress = true
                                if (cmd != 0x06 &&
                                    cmd != 0x09 &&
                                    cmd != 0x13 &&
                                    cmd != 0x25 &&
                                    cmd != 0x27
                                ) {
                                    synchronized(ackLock) {
                                        dartAckExpectedSeq = ackSeq
                                        dartAckPendingUntil = SystemClock.uptimeMillis() + 600L
                                    }
                                }
                                if (cmd == 0x06) {
                                    scanner.armImageGuard(3_000L)
                                }
                                acquired = true
                            }
                        }
                        if (acquired) {
                            if (dartTransferActive) {
                                dartTransferUntil = maxOf(
                                    dartTransferUntil,
                                    SystemClock.uptimeMillis() + 8_000L,
                                )
                            }
                            val ok = try {
                                writeOrReport(bytes, bypassImageGuard = (cmd == 0x06))
                            } finally {
                                synchronized(dartTransferLock) {
                                    dartWriteInProgress = false
                                    dartTransferLock.notifyAll()
                                }
                            }
                            if (!ok) {
                                if (cmd == 0x06) scanner.clearImageGuard()
                                synchronized(ackLock) {
                                    dartAckPendingUntil = 0L
                                    ackLock.notifyAll()
                                }
                            }
                            mainHandler.post { done(ok) }
                            return@execute
                        }
                    }
                } else if (cmd == 0x09 || cmd == 0x13) {
                    mainHandler.post { done(false) }
                    return@execute
                }
                Thread.sleep(15)
            }
            mainHandler.post { done(false) }
        }
    }

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
        connectExecutor.shutdownNow()
        sendExecutor.shutdownNow()
        txExecutor.shutdownNow()
        super.onDestroy()
    }

    // ---- 连接 ---------------------------------------------------------------

    private fun connect(address: String) {
        if (connected.get() && address.equals(currentAddress, ignoreCase = true)) {
            // Flutter 重启后直接挂到已有连接上，不再重连一次。
            emitState(true, null)
            emitBusy(bitmapBusy.get())
            updateNotification(statusText)
            finishConnect(true, null)
            messageMode = false
            watchMusicActive = false
            WatchNotificationListener.ensureBound(applicationContext)
            mainHandler.postDelayed({
                if (!connected.get()) return@postDelayed
                if (WatchNotificationListener.live != null) {
                    WatchNotificationListener.rescan()
                } else {
                    pushPersistedIfConnected()
                }
            }, 650)
            return
        }
        stopping.set(false)
        connectExecutor.execute {
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
                lastWatchMsgCount = -1
                lastPushedSignature = ""
                urgentDetailSlot = -1
                prefs.edit().putString(KEY_ADDRESS, address).apply()
                startReadLoop(candidate)
                statusText = "已连接手表"
                Log.d("WatchLink", "已连接 $address")
                messageMode = false
                watchMusicActive = false
                // 趁 Dart 还没开始握手先把校时和 0x2D 00 走完并收齐 ack，避免撞帧。
                writeWithAck(buildTimeFrame(), timeoutMs = 800L)
                writeWithAck(buildFrame(0x2D, byteArrayOf(0x00)), timeoutMs = 800L)
                emitState(true, null)
                updateNotification(statusText)
                finishConnect(true, null)
                startKeepAlive()
                WatchNotificationListener.ensureBound(applicationContext)
                mainHandler.postDelayed({
                    if (!connected.get()) return@postDelayed
                    if (WatchNotificationListener.live != null) {
                        WatchNotificationListener.rescan()
                    } else {
                        pushPersistedIfConnected()
                    }
                }, 650)
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
                    synchronized(ackLock) { ackLock.notifyAll() }
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
        synchronized(dartTransferLock) {
            dartTransferActive = false
            dartTransferUntil = 0L
            dartWriteInProgress = false
            dartTransferLock.notifyAll()
        }
        synchronized(ackLock) {
            dartAckPendingUntil = 0L
            ackLock.notifyAll()
        }
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

    private fun write(bytes: ByteArray, bypassImageGuard: Boolean = false): Boolean {
        if (!connected.get()) return false
        if (!bypassImageGuard && scanner.imageGuardActive()) return false
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
    private fun writeOrReport(bytes: ByteArray, bypassImageGuard: Boolean = false): Boolean {
        val ok = write(bytes, bypassImageGuard)
        if (!ok && connected.get() && (bypassImageGuard || !scanner.imageGuardActive())) {
            reportLinkLoss()
        }
        return ok
    }

    private fun reportLinkLoss() {
        if (!connected.getAndSet(false)) return
        synchronized(dartTransferLock) {
            dartTransferActive = false
            dartTransferUntil = 0L
            dartWriteInProgress = false
            dartTransferLock.notifyAll()
        }
        synchronized(ackLock) {
            dartAckPendingUntil = 0L
            ackLock.notifyAll()
        }
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

    private fun buildTimeFrame(): ByteArray {
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
        return buildFrame(0x01, payload)
    }

    /**
     * 手表 4 秒收不到合法帧就会显示“无连接”，这里补心跳。
     * 只有 1.5 秒内没有任何收发、且 Dart 侧未在传输时才发，避免和 App 的命令抢 ack。
     */
    private fun startKeepAlive() {
        keepAliveRunnable?.let { mainHandler.removeCallbacks(it) }
        val runnable = object : Runnable {
            override fun run() {
                if (!connected.get()) return
                val now = SystemClock.uptimeMillis()
                if (notificationForward && now - lastListenerCheckMs > 10_000L) {
                    lastListenerCheckMs = now
                    WatchNotificationListener.ensureBound(applicationContext)
                }
                if (!bitmapBusy.get() &&
                    !isDartTransferValid() &&
                    !scanner.imageGuardActive() &&
                    now - lastTraffic > 1_500
                ) {
                    txExecutor.execute {
                        if (!connected.get() ||
                            isDartTransferValid() ||
                            scanner.imageGuardActive() ||
                            SystemClock.uptimeMillis() - lastTraffic <= 1_500
                        ) {
                            return@execute
                        }
                        withBitmapTransfer(notifyFlutterBusy = false) {
                            if (SystemClock.uptimeMillis() - lastTraffic > 1_500) {
                                writeWithAck(buildFrame(0x7F, ByteArray(0)), timeoutMs = 1_500L)
                            }
                        }
                    }
                }
                mainHandler.postDelayed(this, 2_000)
            }
        }
        keepAliveRunnable = runnable
        mainHandler.postDelayed(runnable, 2_000)
    }

    // ---- 媒体 ---------------------------------------------------------------

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

    private fun onWatchExtPoolState(flags: Int, msgCount: Int) {
        val active = (flags and 0x10) != 0
        val prevCount = lastWatchMsgCount
        lastWatchMsgCount = msgCount
        val clearedOnWatch = prevCount > 0 &&
            msgCount == 0 &&
            SystemClock.uptimeMillis() > phoneClearGuardUntil
        mainHandler.post {
            if (clearedOnWatch) {
                clearNotificationsFromWatch()
            }
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
                txExecutor.execute { sendBitmap(WatchTextRasterizer.render(page)) }
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

    private inline fun <T> withBitmapTransfer(
        notifyFlutterBusy: Boolean = true,
        block: () -> T,
    ): T {
        val topLevel = bitmapDepth == 0
        var notifiedBusy = false
        if (topLevel) {
            val waitDeadline = SystemClock.uptimeMillis() + 12_000L
            while (connected.get()) {
                var ready = false
                synchronized(txCoordLock) {
                    if (!isDartTransferValid() &&
                        !scanner.imageGuardActive() &&
                        !dartWriteInProgress
                    ) {
                        bitmapBusy.set(true)
                        ready = true
                    }
                }
                if (ready) break
                val rem = waitDeadline - SystemClock.uptimeMillis()
                if (rem <= 0L) {
                    synchronized(txCoordLock) {
                        bitmapBusy.set(true)
                    }
                    break
                }
                synchronized(dartTransferLock) {
                    if (isDartTransferValid() ||
                        scanner.imageGuardActive() ||
                        dartWriteInProgress
                    ) {
                        dartTransferLock.wait(minOf(45L, rem))
                    }
                }
            }
            if (!bitmapBusy.get()) {
                synchronized(txCoordLock) { bitmapBusy.set(true) }
            }
            if (notifyFlutterBusy) {
                emitBusy(true)
                notifiedBusy = true
            }
            // 等刚刚从 Flutter 发出、还未回 ack 的单帧完成应答，避免错认前一次 ok。
            synchronized(ackLock) {
                while (connected.get() && ackSeq == dartAckExpectedSeq) {
                    val rem = dartAckPendingUntil - SystemClock.uptimeMillis()
                    if (rem <= 0L) break
                    ackLock.wait(rem)
                }
            }
        }
        bitmapDepth++
        return try {
            block()
        } finally {
            bitmapDepth--
            if (bitmapDepth == 0) {
                synchronized(txCoordLock) { bitmapBusy.set(false) }
                if (notifiedBusy) emitBusy(false)
            }
        }
    }

    /** 发一帧并等待手表回复 ok/bad，防止连发爆掉 AT32 的 256B 串口缓冲（EEPROM 写页期间约 20..100ms）。 */
    private fun writeWithAck(
        frame: ByteArray,
        timeoutMs: Long = 3_000L,
        retryBad: Boolean = false,
        retryTimeout: Boolean = false,
    ): Boolean {
        var badRetries = if (retryBad) 2 else 0
        var timeoutRetries = if (retryTimeout) 1 else 0
        var tries = 0
        while (tries < 80) {
            if (!connected.get()) return false
            waitForCapture()
            if (!connected.get()) return false
            val seqBefore: Long
            synchronized(ackLock) { seqBefore = ackSeq }
            if (writeOrReport(frame)) {
                val deadline = SystemClock.uptimeMillis() + timeoutMs
                synchronized(ackLock) {
                    while (ackSeq == seqBefore && connected.get()) {
                        val rem = deadline - SystemClock.uptimeMillis()
                        if (rem <= 0L) break
                        ackLock.wait(rem)
                    }
                    if (ackSeq != seqBefore) {
                        if (ackResult) return true
                        if (badRetries > 0) {
                            badRetries--
                            tries++
                            continue
                        }
                        return false
                    }
                }
                if (timeoutRetries > 0 && connected.get() && !scanner.imageGuardActive()) {
                    timeoutRetries--
                    tries++
                    continue
                }
                return false
            }
            if (!connected.get() || !scanner.imageGuardActive()) return false
            Thread.sleep(50)
            tries++
        }
        return false
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
        withBitmapTransfer {
            if (bits.isEmpty() || bits.size % 128 != 0) return@withBitmapTransfer false
            if (clearLyric) {
                // 新一帧点阵到达前，手表会回退到 0x03 的文字歌词（小字库容易出方框）。
                writeWithAck(
                    buildFrame(0x03, "...".toByteArray(Charsets.UTF_8)),
                    retryBad = true,
                )
            }
            val chunks = bits.size / 128
            var rleSaved = 0
            var rawBlocks = 0
            for (chunk in 0 until chunks) {
                if (!connected.get()) return@withBitmapTransfer false
                waitForCapture()
                if (!connected.get()) return@withBitmapTransfer false

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

                if (!writeWithAck(frame, retryBad = true, retryTimeout = true)) {
                    Log.w("WatchLink", "点阵 type=$maskType chunk=$chunk/$chunks 发送失败")
                    return@withBitmapTransfer false
                }
            }
            Log.d(
                "WatchLink",
                "点阵 type=$maskType 完成：RLE 省 ${rleSaved}B，原始块 $rawBlocks",
            )
            true
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
        txExecutor.execute {
            withBitmapTransfer(notifyFlutterBusy = false) {
                writeWithAck(
                    buildFrame(0x04, byteArrayOf(if (playing) 1 else 0)),
                    timeoutMs = 1_500L,
                )
            }
        }
    }

    // ---- 通知缓存与消息页 ---------------------------------------------------

    private fun loadPersistedMessages() {
        synchronized(messagesLock) {
            synchronized(offlineStoreLock) {
                val restored = loadOfflineList(applicationContext)
                messages.clear()
                for (entry in restored) {
                    messages.addLast(entry)
                }
                trimMessagesLocked()
            }
        }
    }

    private fun persistMessagesLocked() {
        val snapshot = messages.toList()
        synchronized(offlineStoreLock) {
            saveOfflineList(applicationContext, snapshot)
        }
    }

    fun clearNotifications() {
        val snapshots = WatchNotificationListener.captureActiveSnapshots()
        NotificationRules.markDismissedBatch(applicationContext, snapshots)
        phoneClearGuardUntil = SystemClock.uptimeMillis() + 3_000L
        synchronized(messagesLock) {
            pushListGen++
            lastPushedSignature = ""
            urgentDetailSlot = -1
            messages.clear()
            persistMessagesLocked()
        }
        if (connected.get()) {
            txExecutor.execute {
                withBitmapTransfer {
                    writeWithAck(buildFrame(0x24, ByteArray(0)), retryBad = true)
                    writeWithAck(buildFrame(0x2D, byteArrayOf(0x00)))
                }
            }
        }
    }

    private fun clearNotificationsFromWatch() {
        val snapshots = WatchNotificationListener.captureActiveSnapshots()
        NotificationRules.markDismissedBatch(applicationContext, snapshots)
        val hadMessages: Boolean
        synchronized(messagesLock) {
            pushListGen++
            lastPushedSignature = ""
            urgentDetailSlot = -1
            hadMessages = messages.isNotEmpty()
            messages.clear()
            persistMessagesLocked()
        }
        if (hadMessages) {
            Log.i("WatchLink", "手表端清空通知，已同步清空手机缓存并记录状态栏基线（${snapshots.size} 条）")
        }
    }

    /** 推送一条置顶消息到手表片外 AT24C256 动态 ROM（0x2D prepend + 行点阵 + 槽位 0 详情）。 */
    private fun pushPrependToWatch(count: Int, item: WatchMessageRasterizer.Item) {
        txExecutor.execute {
            if (!connected.get()) return@execute
            val rowBits = WatchMessageRasterizer.row(item)
            val detailBits = WatchMessageRasterizer.detail(item)
            withBitmapTransfer {
                if (!connected.get()) return@withBitmapTransfer
                if (!writeWithAck(
                        buildFrame(0x2D, byteArrayOf(0x01, count.coerceIn(1, 20).toByte())),
                        retryBad = true,
                        retryTimeout = true,
                    )
                ) {
                    Log.w("WatchLink", "0x2D prepend 头帧失败：${item.app}")
                    return@withBitmapTransfer
                }
                val chunks = (rowBits.size + 127) / 128
                for (sub in 0 until chunks) {
                    val start = sub * 128
                    val len = minOf(128, rowBits.size - start)
                    val payload = ByteArray(2 + len)
                    payload[0] = 0x02
                    payload[1] = sub.toByte()
                    System.arraycopy(rowBits, start, payload, 2, len)
                    if (!writeWithAck(buildFrame(0x2D, payload), retryBad = true, retryTimeout = true)) {
                        Log.w("WatchLink", "0x2D 行点阵 chunk=$sub 失败：${item.app}")
                        return@withBitmapTransfer
                    }
                }
                if (sendDetailSlot(0, detailBits)) {
                    writeWithAck(buildFrame(0x2D, byteArrayOf(0x00)))
                    lastPushedSignature = ""
                    Log.i("WatchLink", "新通知已送达手表：[${item.categoryLabel}] ${item.app} - ${item.title}")
                } else {
                    Log.w("WatchLink", "新通知详情槽 0 下发失败：${item.app}")
                }
            }
        }
    }

    private fun sendDetailSlot(slot: Int, bits: ByteArray, backgroundWarm: Boolean = false): Boolean =
        withBitmapTransfer {
            if (bits.isEmpty() || bits.size % 128 != 0 || slot !in 0..19) return@withBitmapTransfer false
            val chunks = bits.size / 128
            for (chunk in 0 until chunks) {
                if (!connected.get()) return@withBitmapTransfer false
                if (backgroundWarm && (isDartTransferValid() || (urgentDetailSlot in 0..19 && urgentDetailSlot != slot))) {
                    return@withBitmapTransfer false
                }
                waitForCapture()
                if (!connected.get()) return@withBitmapTransfer false
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
                if (!writeWithAck(frame, retryBad = true, retryTimeout = true)) {
                    Log.w("WatchLink", "详情槽 slot=$slot chunk=$chunk/$chunks 下发失败")
                    return@withBitmapTransfer false
                }
            }
            true
        }

    data class IncomingMessage(
        val app: String,
        val title: String,
        val text: String,
        val key: String,
        val pkg: String,
        val isGroupSummary: Boolean = false,
        val groupKey: String? = null,
        val categoryId: String = "",
        val categoryLabel: String = "",
    )

    /** 记录从 Flutter UI 手动发送的消息，避免打开消息页或重扫通知时将其冲掉。 */
    fun recordManualMessage(app: String, title: String, text: String) {
        val classification = NotificationClassifier.classify(
            NotificationSignal(
                pkg = MANUAL_PKG,
                appName = app,
                title = title,
                text = text,
            ),
        )
        synchronized(messagesLock) {
            val item = WatchMessageRasterizer.Item(
                app = app,
                title = title,
                text = text,
                categoryId = classification.category.id,
                categoryLabel = classification.category.label,
            )
            messages.addFirst(
                CachedMessage(
                    key = "manual:${SystemClock.uptimeMillis()}",
                    pkg = MANUAL_PKG,
                    item = item,
                ),
            )
            lastPushedSignature = ""
            trimMessagesLocked()
            persistMessagesLocked()
        }
    }

    /** 当通知监听尚未绑定时，仍把离线/持久化缓存同步给已连接的手表。 */
    fun pushPersistedIfConnected() {
        if (!notificationForward || !connected.get()) return
        val hasMsgs = synchronized(messagesLock) { messages.isNotEmpty() }
        if (hasMsgs) {
            Log.i("WatchLink", "推送本地已缓存消息到手表")
            pushFullList()
        }
    }

    /** 用通知栏当前内容替换缓存，并整表推给手表；通知栏为空时保留现有/片外历史不清空。 */
    fun syncMessages(incoming: List<IncomingMessage>) {
        if (!notificationForward) return
        val shouldPush: Boolean
        synchronized(messagesLock) {
            if (incoming.isEmpty()) {
                Log.i("WatchLink", "通知栏暂无活跃消息，保留已有 ${messages.size} 条与手表持久化历史")
                shouldPush = messages.isNotEmpty() && connected.get()
            } else {
                val manual = messages.filter { it.pkg == MANUAL_PKG }
                messages.clear()
                for (m in manual) {
                    messages.addLast(m)
                }
                val seenMergeKeys = HashSet<String>()
                for (item in incoming) {
                    val mk = mergeKey(item.pkg, item.categoryId)
                    if (NotificationRules.merge && !seenMergeKeys.add(mk)) {
                        continue
                    }
                    messages.addLast(
                        CachedMessage(
                            key = item.key,
                            pkg = item.pkg,
                            item = WatchMessageRasterizer.Item(
                                app = item.app,
                                title = item.title,
                                text = item.text,
                                categoryId = item.categoryId,
                                categoryLabel = item.categoryLabel,
                            ),
                            isGroupSummary = item.isGroupSummary,
                            groupKey = item.groupKey,
                        ),
                    )
                }
                trimMessagesLocked()
                persistMessagesLocked()
                Log.i("WatchLink", "同步消息 ${messages.size} 条，连接=${connected.get()}")
                shouldPush = connected.get() && messages.isNotEmpty()
            }
        }
        if (shouldPush) pushFullList()
    }

    private fun listSignature(items: List<WatchMessageRasterizer.Item>): String =
        items.joinToString("\n") { "${it.categoryId}|${it.app}|${it.title}|${it.text}" }

    private fun pushFullList(notifyAlert: Boolean = false, force: Boolean = false) {
        val scheduledSig: String
        val gen = synchronized(messagesLock) {
            if (messages.isEmpty()) return
            val sig = listSignature(messages.map { it.item })
            if (!notifyAlert && !force && sig == lastPushedSignature) {
                Log.d("WatchLink", "消息列表无变化，跳过重复整表推送（${messages.size} 条）")
                return
            }
            lastPushedSignature = sig
            scheduledSig = sig
            ++pushListGen
        }
        txExecutor.execute {
            if (!connected.get()) {
                synchronized(messagesLock) {
                    if (lastPushedSignature == scheduledSig) lastPushedSignature = ""
                }
                return@execute
            }
            if (!notifyAlert && !force && gen != pushListGen) return@execute
            val items = synchronized(messagesLock) { messages.map { it.item } }
            if (items.isEmpty()) return@execute
            val sig = listSignature(items)
            withBitmapTransfer {
                if (!connected.get()) return@withBitmapTransfer
                if (notifyAlert) {
                    writeWithAck(buildFrame(0x2D, byteArrayOf(0x03)), retryBad = true)
                }
                if (!writeWithAck(
                        buildFrame(0x25, byteArrayOf(items.size.coerceIn(0, 20).toByte())),
                        retryBad = true,
                        retryTimeout = true,
                    )
                ) {
                    synchronized(messagesLock) {
                        if (lastPushedSignature == scheduledSig) lastPushedSignature = ""
                    }
                    Log.w("WatchLink", "0x25 消息数量帧失败（count=${items.size}）")
                    return@withBitmapTransfer
                }
                if (sendMaskChunks(0x22, WatchMessageRasterizer.list(items), 2)) {
                    lastPushedSignature = sig
                    // 仅预热槽 0 详情，其余槽位由手表点开时通过 0x15 按需拉取；
                    // 避免连续后台预热 1..7 槽详情（每槽暂存需 2.6KB）撑爆 AT24C256 动态池并反过来驱逐消息行。
                    sendDetailSlot(0, WatchMessageRasterizer.detail(items.first()))
                    writeWithAck(buildFrame(0x2D, byteArrayOf(0x00)))
                    Log.i("WatchLink", "消息列表整表已送达手表（${items.size} 条）")
                } else {
                    synchronized(messagesLock) {
                        if (lastPushedSignature == scheduledSig) lastPushedSignature = ""
                    }
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
        isGroupSummary: Boolean = false,
        groupKey: String? = null,
        categoryId: String = "",
        categoryLabel: String = "",
    ) {
        if (!notificationForward) return
        val item: WatchMessageRasterizer.Item
        val hadSame: Boolean
        val count: Int
        val mk = mergeKey(pkg, categoryId)
        synchronized(messagesLock) {
            val unchanged = messages.any {
                it.key == key && it.item.app == app &&
                    it.item.title == title && it.item.text == text &&
                    it.item.categoryId == categoryId
            }
            if (unchanged) return
            if (!isGroupSummary && !groupKey.isNullOrEmpty()) {
                messages.removeAll { it.isGroupSummary && it.pkg == pkg && it.groupKey == groupKey }
            }
            hadSame = messages.any {
                it.key == key || (NotificationRules.merge && mergeKey(it.pkg, it.item.categoryId) == mk)
            }
            messages.removeAll { it.key == key }
            if (NotificationRules.merge) {
                // 同应用同分类只留最新一条，不同分类（如聊天与支付）并存互不覆盖。
                messages.removeAll { mergeKey(it.pkg, it.item.categoryId) == mk }
            }
            item = WatchMessageRasterizer.Item(app, title, text, categoryId, categoryLabel)
            messages.addFirst(CachedMessage(key, pkg, item, isGroupSummary, groupKey))
            trimMessagesLocked()
            persistMessagesLocked()
            count = messages.size
        }
        if (connected.get()) {
            if (!hadSame) {
                pushPrependToWatch(count, item)
            } else {
                pushFullList(notifyAlert = true)
            }
        }
    }

    fun removeNotification(key: String) {
        synchronized(messagesLock) {
            if (messages.removeAll { it.key == key }) {
                lastPushedSignature = ""
                persistMessagesLocked()
            }
        }
    }

    /** 规则修改后立即把缓存收紧到新的条数上限，并重扫通知栏以匹配新规则。 */
    fun applyMessageRules() {
        synchronized(messagesLock) {
            lastPushedSignature = ""
            trimMessagesLocked()
            persistMessagesLocked()
        }
        if (connected.get() && notificationForward) {
            mainHandler.post {
                WatchNotificationListener.ensureBound(applicationContext)
                WatchNotificationListener.rescan()
            }
        }
    }

    private fun trimMessagesLocked() {
        while (messages.size > NotificationRules.maxMessages) {
            messages.removeLast()
        }
    }

    fun messageCount(): Int = synchronized(messagesLock) { messages.size }

    /** 手表打开一级消息页：若本机有缓存则同步点阵，无缓存时保留手表历史/已清空状态不强行拉起状态栏。 */
    private fun onMessageListRequested() {
        mainHandler.post {
            messageMode = true
            mediaRunnable?.let { mainHandler.removeCallbacks(it) }
            mediaRunnable = null
            val currentCount = synchronized(messagesLock) { messages.size }
            if (currentCount == 0) {
                Log.i("WatchLink", "手表要消息列表：本机当前 0 条，保持已清空/手表本地历史")
                return@post
            }
            Log.i("WatchLink", "手表要消息列表：$currentCount 条")
            pushFullList(force = true)
        }
    }

    /** 选中某条消息：本机存在缓存时优先刷新 128×160 详情点阵；否则沿用手表本地动态 ROM。 */
    private fun onMessageDetailRequested(index: Int) {
        mainHandler.post {
            messageMode = true
            val item = synchronized(messagesLock) { messages.getOrNull(index)?.item } ?: return@post
            urgentDetailSlot = index
            txExecutor.execute {
                try {
                    sendDetailSlot(index, WatchMessageRasterizer.detail(item))
                } finally {
                    if (urgentDetailSlot == index) {
                        urgentDetailSlot = -1
                    }
                }
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
    val isGroupSummary: Boolean = false,
    val groupKey: String? = null,
)

/**
 * 只挑控制/消息请求帧；抓屏（0x11）是 40KB 裸流，必须整帧跳过，
 * 否则里面的随机字节会被误判成控制命令。
 */
private class FrameScanner(
    private val onFrame: (Int, Int, Int) -> Unit,
    private val onAck: (Boolean) -> Unit,
) {
    private var buffer = ByteArray(1024)
    private var size = 0
    private val ackLine = StringBuilder(32)
    private var ackLineOverflow = false

    @Volatile
    private var imageUntil = 0L

    fun imageGuardActive(): Boolean =
        SystemClock.uptimeMillis() < imageUntil

    fun armImageGuard(ms: Long) {
        val target = SystemClock.uptimeMillis() + ms
        if (target > imageUntil) imageUntil = target
    }

    fun clearImageGuard() {
        imageUntil = 0L
    }

    fun reset() {
        size = 0
        ackLine.setLength(0)
        ackLineOverflow = false
        imageUntil = 0L
    }

    private fun feedTextByte(b: Byte) {
        val v = b.toInt() and 0xFF
        if (v == '\r'.code || v == '\n'.code) {
            if (!ackLineOverflow && ackLine.isNotEmpty()) {
                val s = ackLine.toString().trim().lowercase()
                if (s == "ok") {
                    onAck(true)
                } else if (s == "bad") {
                    onAck(false)
                }
            }
            ackLine.setLength(0)
            ackLineOverflow = false
        } else if (v in 0x20..0x7E) {
            if (!ackLineOverflow) {
                if (ackLine.length < 32) {
                    ackLine.append(v.toChar())
                } else {
                    ackLine.setLength(0)
                    ackLineOverflow = true
                }
            }
        }
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
            val textEnd = if (mark < 0) size else mark
            for (i in index until textEnd) {
                feedTextByte(buffer[i])
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
                if (payload > MAX_FRAME_PAYLOAD) {
                    index += 1
                    continue
                }
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
                    onFrame(cmd, first, 0)
                } else if (cmd == 0x2E && payload >= 13) {
                    val msgCount = buffer[index + header + 10].toInt() and 0xFF
                    val flags = buffer[index + header + 12].toInt() and 0xFF
                    onFrame(cmd, flags, msgCount)
                }
                if (cmd == 0x11) {
                    imageUntil = SystemClock.uptimeMillis() + 150
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
        private const val MAX_FRAME_PAYLOAD = 64
    }
}
