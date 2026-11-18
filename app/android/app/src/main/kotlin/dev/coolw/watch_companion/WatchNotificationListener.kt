package dev.coolw.watch_companion

import android.app.Notification
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import android.view.KeyEvent

/**
 * 读取系统媒体会话（汽水音乐、网易云、B 站等）并转发通知。
 *
 * 需要用户在系统里手动开启“通知使用权”，这是 Android 读取其他应用
 * MediaSession 的唯一官方入口；MediaSessionManager.getActiveSessions
 * 也依赖这个组件。
 */
class WatchNotificationListener : NotificationListenerService() {
    companion object {
        /** 广告和纯壳进程。应用商店更新、抖音这类通知要留下。 */
        val ignoredPackages = setOf(
            "com.android.shell",
            "com.android.systemui",
            "com.miui.systemAdSolution",
            "com.xiaomi.shop",
        )

        @Volatile
        var live: WatchNotificationListener? = null

        fun hasNotificationAccess(context: Context): Boolean {
            val appCtx = context.applicationContext
            val component = ComponentName(appCtx, WatchNotificationListener::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                val nm = appCtx.getSystemService(NotificationManager::class.java)
                if (nm != null) {
                    return nm.isNotificationListenerAccessGranted(component)
                }
            }
            val enabled = Settings.Secure.getString(
                appCtx.contentResolver,
                "enabled_notification_listeners",
            ) ?: return false
            return enabled.split(':').any { entry ->
                val parsed = ComponentName.unflattenFromString(entry)
                parsed == component || parsed?.packageName == appCtx.packageName
            }
        }

        fun ensureBound(context: Context) {
            if (live != null) return
            val appCtx = context.applicationContext
            if (!hasNotificationAccess(appCtx)) return
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                try {
                    requestRebind(ComponentName(appCtx, WatchNotificationListener::class.java))
                    Log.i("WatchLink", "已请求系统重新绑定通知监听服务")
                } catch (error: Exception) {
                    Log.w("WatchLink", "请求绑定通知监听失败：${error.message}")
                }
            }
        }

        fun rescan() {
            val activeListener = live
            if (activeListener != null) {
                activeListener.seedActive()
            } else {
                Log.i("WatchLink", "通知监听服务未绑定，无法扫描活动通知")
                WatchLinkService.instance?.pushPersistedIfConnected()
            }
        }
    }

    private var controller: MediaController? = null
    private var lastNotificationKey = ""
    private var lastNotificationAt = 0L

    private val component: ComponentName by lazy {
        ComponentName(this, WatchNotificationListener::class.java)
    }

    private val sessionsListener =
        MediaSessionManager.OnActiveSessionsChangedListener { controllers ->
            refresh(controllers)
        }

    private val callback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) = publish()

        override fun onPlaybackStateChanged(state: PlaybackState?) = publish()

        override fun onSessionDestroyed() {
            controller = null
            MediaBridge.sessionController = null
        }
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        live = this
        NotificationRules.load(applicationContext)
        Log.i("WatchLink", "通知监听服务已连接")
        try {
            getSystemService(MediaSessionManager::class.java)
                ?.addOnActiveSessionsChangedListener(sessionsListener, component)
        } catch (_: Exception) {
        }
        refreshSessions()
        seedActive()
    }

    override fun onDestroy() {
        if (live === this) live = null
        super.onDestroy()
    }

    override fun onListenerDisconnected() {
        if (live === this) live = null
        try {
            getSystemService(MediaSessionManager::class.java)
                ?.removeOnActiveSessionsChangedListener(sessionsListener)
        } catch (_: Exception) {
        }
        super.onListenerDisconnected()
        ensureBound(applicationContext)
    }

    private fun refreshSessions() {
        val controllers = try {
            getSystemService(MediaSessionManager::class.java)
                ?.getActiveSessions(component)
        } catch (_: Exception) {
            null
        }
        refresh(controllers)
    }

    private fun refresh(controllers: List<MediaController>?) {
        val list = controllers.orEmpty()
        val chosen = list.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: list.firstOrNull()

        if (chosen?.sessionToken == controller?.sessionToken) {
            publish()
            return
        }
        controller?.unregisterCallback(callback)
        controller = chosen
        chosen?.registerCallback(callback)
        MediaBridge.sessionController = chosen
        publish()
    }

    private fun publish() {
        val active = controller ?: return
        MediaBridge.updateFrom(active)
    }

    private fun firstNonBlank(vararg values: CharSequence?): String {
        for (value in values) {
            val s = value?.toString()?.trim()
            if (!s.isNullOrEmpty()) return s
        }
        return ""
    }

    private fun extractLatestMessaging(extras: Bundle): Pair<String, String> {
        @Suppress("DEPRECATION")
        val raw = extras.getParcelableArray(Notification.EXTRA_MESSAGES) ?: return "" to ""
        for (i in raw.size - 1 downTo 0) {
            val bundle = raw[i] as? Bundle ?: continue
            val msgText = bundle.getCharSequence("text")?.toString()?.trim().orEmpty()
            if (msgText.isEmpty()) continue
            val sender = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                bundle.getParcelable("sender_person", android.app.Person::class.java)
                    ?.name
                    ?.toString()
                    ?.trim()
                    .orEmpty()
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                @Suppress("DEPRECATION")
                (bundle.getParcelable("sender_person") as? android.app.Person)
                    ?.name
                    ?.toString()
                    ?.trim()
                    .orEmpty()
            } else {
                ""
            }.ifBlank {
                bundle.getCharSequence("sender")?.toString()?.trim().orEmpty()
            }
            return sender to msgText
        }
        return "" to ""
    }

    private fun extractTitleText(payload: Notification): Pair<String, String> {
        val extras = payload.extras
        if (extras == null) {
            val ticker = payload.tickerText?.toString()?.trim().orEmpty()
            return "" to ticker
        }
        var title = firstNonBlank(
            extras.getCharSequence(Notification.EXTRA_TITLE),
            extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE),
            extras.getCharSequence(Notification.EXTRA_TITLE_BIG),
        )
        var text = firstNonBlank(
            extras.getCharSequence(Notification.EXTRA_BIG_TEXT),
            extras.getCharSequence(Notification.EXTRA_TEXT),
        )
        if (title.isBlank() || text.isBlank()) {
            val (msgSender, msgText) = extractLatestMessaging(extras)
            if (title.isBlank() && msgSender.isNotBlank()) {
                title = msgSender
            }
            if (text.isBlank() && msgText.isNotBlank()) {
                text = if (msgSender.isNotBlank() && msgSender != title) {
                    "$msgSender: $msgText"
                } else {
                    msgText
                }
            }
        }
        if (text.isBlank()) {
            val lines = extras.getCharSequenceArray(Notification.EXTRA_TEXT_LINES)
            if (lines != null && lines.isNotEmpty()) {
                text = lines
                    .mapNotNull { it?.toString()?.trim()?.takeIf { s -> s.isNotEmpty() } }
                    .joinToString("\n")
            }
        }
        if (text.isBlank()) {
            text = firstNonBlank(
                extras.getCharSequence(Notification.EXTRA_SUB_TEXT),
                extras.getCharSequence(Notification.EXTRA_INFO_TEXT),
                extras.getCharSequence(Notification.EXTRA_SUMMARY_TEXT),
            )
        }
        if (title.isBlank() && text.isBlank()) {
            text = payload.tickerText?.toString()?.trim().orEmpty()
        }
        return title to text
    }

    private fun safeActiveNotifications(): Array<StatusBarNotification>? = try {
        activeNotifications
    } catch (_: Exception) {
        null
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        val notification = sbn ?: return
        val payload = notification.notification ?: return
        val (title, text) = extractTitleText(payload)
        val activeList = safeActiveNotifications()
        if (!acceptNotification(notification, title, text, activeList)) return

        val key = "${notification.key}\u0000$title\u0000$text"
        val now = System.currentTimeMillis()
        if (key == lastNotificationKey && now - lastNotificationAt < 10_000) {
            return
        }
        if (cache(notification, title, text)) {
            lastNotificationKey = key
            lastNotificationAt = now
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        val removed = sbn ?: return
        val service = WatchLinkService.instance
        if (service != null) {
            service.removeNotification(removed.key)
        } else {
            WatchLinkService.removeOfflineNotification(applicationContext, removed.key)
        }
    }

    /** 把通知栏里当前还能看到的消息一次性交给前台服务。 */
    private fun seedActive() {
        NotificationRules.load(applicationContext)
        val active = safeActiveNotifications() ?: return
        val incoming = active
            .sortedByDescending { it.postTime }
            .mapNotNull { notification ->
                val payload = notification.notification ?: return@mapNotNull null
                val (title, text) = extractTitleText(payload)
                if (!acceptNotification(notification, title, text, active)) {
                    return@mapNotNull null
                }
                val app = appLabel(notification.packageName)
                val kind = classifyNotification(notification.packageName, payload.category)
                NotificationRules.rememberApp(this, notification.packageName, "$kind · $app")
                if (!NotificationRules.allows(notification.packageName)) return@mapNotNull null
                WatchLinkService.IncomingMessage(
                    app = "$kind · $app",
                    title = title,
                    text = text,
                    key = notification.key,
                    pkg = notification.packageName,
                    isGroupSummary = (payload.flags and Notification.FLAG_GROUP_SUMMARY) != 0,
                    groupKey = notification.groupKey,
                )
            }
        Log.i("WatchLink", "通知栏现有 ${active.size} 条，可送手表 ${incoming.size} 条")
        val service = WatchLinkService.instance
        if (service != null) {
            service.syncMessages(incoming)
        } else {
            WatchLinkService.syncOfflineMessages(applicationContext, incoming)
            Log.i("WatchLink", "前台服务未启动，已暂存 ${incoming.size} 条活动通知到本地缓存")
        }
    }

    private fun acceptNotification(
        notification: StatusBarNotification,
        title: String,
        text: String,
        activeList: Array<StatusBarNotification>? = null,
    ): Boolean {
        if (notification.packageName == packageName) return false
        if (notification.packageName in ignoredPackages) return false
        val payload = notification.notification ?: return false
        if ((payload.flags and Notification.FLAG_ONGOING_EVENT) != 0 &&
            payload.extras?.containsKey(Notification.EXTRA_MEDIA_SESSION) == true
        ) {
            return false
        }
        if ((payload.flags and Notification.FLAG_FOREGROUND_SERVICE) != 0 &&
            payload.category != Notification.CATEGORY_CALL
        ) {
            return false
        }
        if ((payload.flags and Notification.FLAG_GROUP_SUMMARY) != 0) {
            val siblings = activeList
            val hasChild = siblings != null && siblings.any { other ->
                other.key != notification.key &&
                    other.packageName == notification.packageName &&
                    other.groupKey == notification.groupKey &&
                    ((other.notification?.flags ?: 0) and Notification.FLAG_GROUP_SUMMARY) == 0 &&
                    other.notification?.let { childPayload ->
                        val (ct, cx) = extractTitleText(childPayload)
                        ct.isNotBlank() || cx.isNotBlank()
                    } == true
            }
            if (hasChild) return false
        }
        if (title.isBlank() && text.isBlank()) return false
        return true
    }

    private fun cache(
        notification: StatusBarNotification,
        title: String,
        text: String,
    ): Boolean {
        val payload = notification.notification ?: return false
        val app = appLabel(notification.packageName)
        val kind = classifyNotification(
            notification.packageName,
            payload.category,
        )
        val labeled = "$kind · $app"
        NotificationRules.load(applicationContext)
        // 即使当前规则不放行也先记住应用，用户才能在列表里勾选它。
        NotificationRules.rememberApp(this, notification.packageName, labeled)
        if (!NotificationRules.allows(notification.packageName)) return false
        Log.i("WatchLink", "缓存通知 [$kind] $app：$title $text")
        val isSummary = (payload.flags and Notification.FLAG_GROUP_SUMMARY) != 0
        // 只进缓存，手表打开消息页时再按需取；不再走 0x02 文字槽（小字库会出方框）。
        val service = WatchLinkService.instance
        return if (service != null) {
            service.cacheNotification(
                labeled,
                title,
                text,
                notification.key,
                notification.packageName,
                isGroupSummary = isSummary,
                groupKey = notification.groupKey,
            )
            true
        } else {
            WatchLinkService.cacheOfflineNotification(
                applicationContext,
                labeled,
                title,
                text,
                notification.key,
                notification.packageName,
                isGroupSummary = isSummary,
                groupKey = notification.groupKey,
            )
        }
    }

    /** 按系统通知类别和应用包名归类，手表列表里能看出是聊天、系统还是提醒。 */
    private fun classifyNotification(pkg: String, category: String?): String {
        when (category) {
            Notification.CATEGORY_MESSAGE,
            Notification.CATEGORY_SOCIAL -> return "聊天"
            Notification.CATEGORY_EMAIL -> return "邮件"
            Notification.CATEGORY_CALL -> return "来电"
            Notification.CATEGORY_ALARM,
            Notification.CATEGORY_REMINDER,
            Notification.CATEGORY_EVENT -> return "提醒"
            Notification.CATEGORY_NAVIGATION -> return "出行"
            Notification.CATEGORY_TRANSPORT -> return "媒体"
            Notification.CATEGORY_SYSTEM,
            Notification.CATEGORY_ERROR,
            Notification.CATEGORY_STATUS,
            Notification.CATEGORY_SERVICE -> return "系统"
            Notification.CATEGORY_PROMO,
            Notification.CATEGORY_RECOMMENDATION -> return "推广"
        }
        val name = pkg.lowercase()
        return when {
            name.contains("mms") ||
                name.contains("messaging") ||
                name.contains("wechat") ||
                name.contains("tencent.mm") ||
                name.contains("aweme") ||
                name.contains("gifmaker") ||
                name.contains("weibo") ||
                name.contains("mobileqq") ||
                name.contains("telegram") ||
                name.contains("whatsapp") -> "聊天"
            name.contains("music") ||
                name.contains("luna") ||
                name.contains("netease") ||
                name.contains("kugou") ||
                name.contains("qqmusic") -> "媒体"
            name.startsWith("com.android.") ||
                name.startsWith("android") ||
                name.startsWith("com.miui.") ||
                name.startsWith("com.xiaomi.") ||
                name.startsWith("com.google.android.") -> "系统"
            else -> "通知"
        }
    }

    private fun appLabel(pkg: String): String = try {
        val info = packageManager.getApplicationInfo(pkg, 0)
        packageManager.getApplicationLabel(info).toString()
    } catch (_: Exception) {
        pkg
    }
}

/** 服务与通知监听共用的媒体状态；App 不在时也能在重连后补推。 */
object MediaBridge {
    data class Snapshot(
        val title: String,
        val artist: String,
        val playing: Boolean,
        val positionMs: Long,
        val durationMs: Long,
    )

    @Volatile
    var current: Snapshot = Snapshot("", "", false, 0, 0)

    @Volatile
    var sessionController: MediaController? = null

    @Volatile
    private var lastLoggedTitle = ""

    fun updateFrom(controller: MediaController) {
        sessionController = controller
        val snapshot = snapshotOf(controller)
        current = snapshot
        WatchLinkService.instance?.onMediaChanged(
            snapshot.title,
            snapshot.artist,
            snapshot.playing,
        )
    }

    /** App 每秒拉一次：标题、歌手、播放态和进度。 */
    fun nowPlaying(context: Context): Map<String, Any?> {
        val controller = sessionController ?: refresh(context)
        val snapshot = if (controller != null) snapshotOf(controller) else current
        if (snapshot.title.isNotEmpty() && snapshot.title != lastLoggedTitle) {
            lastLoggedTitle = snapshot.title
            Log.d(
                "WatchLink",
                "当前媒体：${snapshot.title} / ${snapshot.artist} " +
                    "playing=${snapshot.playing}",
            )
        }
        return mapOf(
            "title" to snapshot.title,
            "artist" to snapshot.artist,
            "playing" to snapshot.playing,
            "positionMs" to snapshot.positionMs,
            "durationMs" to snapshot.durationMs,
        )
    }

    /** 手表 0x10：1 播放 2 暂停 3 下一首 4 上一首。 */
    fun control(action: Int, context: Context) {
        val controller = sessionController ?: refresh(context)
        if (controller == null) {
            val keyCode = when (action) {
                1 -> KeyEvent.KEYCODE_MEDIA_PLAY
                2 -> KeyEvent.KEYCODE_MEDIA_PAUSE
                3 -> KeyEvent.KEYCODE_MEDIA_NEXT
                4 -> KeyEvent.KEYCODE_MEDIA_PREVIOUS
                else -> 0
            }
            val am = context.getSystemService(AudioManager::class.java)
            if (keyCode != 0 && am != null) {
                Log.d("WatchLink", "媒体控制 action=$action 走系统媒体键广播")
                am.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
                am.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
            } else {
                Log.d("WatchLink", "媒体控制 $action 失败：没有活动会话（检查通知使用权）")
            }
            return
        }
        Log.d("WatchLink", "媒体控制 action=$action")
        val controls = controller.transportControls
        when (action) {
            1 -> controls.play()
            2 -> controls.pause()
            3 -> controls.skipToNext()
            4 -> controls.skipToPrevious()
        }
        if (action == 1 || action == 2) {
            current = current.copy(playing = action == 1)
            WatchLinkService.instance?.onMediaChanged(
                current.title,
                current.artist,
                current.playing,
            )
        }
    }

    fun controlAction(action: String, context: Context) {
        when (action) {
            "play" -> control(1, context)
            "pause" -> control(2, context)
            "next" -> control(3, context)
            "previous" -> control(4, context)
        }
    }

    fun seekTo(positionMs: Long, context: Context) {
        val controller = sessionController ?: refresh(context) ?: return
        controller.transportControls.seekTo(positionMs.coerceAtLeast(0))
    }

    /** 通知使用权开启后，按官方入口重新拿一次会话；控制失败时兜底。 */
    fun refresh(context: Context): MediaController? {
        return try {
            val manager = context.getSystemService(MediaSessionManager::class.java)
                ?: return null
            val component = ComponentName(context, WatchNotificationListener::class.java)
            val sessions = manager.getActiveSessions(component)
            val chosen = sessions.firstOrNull {
                it.playbackState?.state == PlaybackState.STATE_PLAYING
            } ?: sessions.firstOrNull()
            if (chosen != null) {
                updateFrom(chosen)
            } else {
                sessionController = null
                current = Snapshot("", "", false, 0, 0)
            }
            chosen
        } catch (error: Exception) {
            Log.d("WatchLink", "读取媒体会话失败：${error.message}")
            null
        }
    }

    private fun snapshotOf(controller: MediaController): Snapshot {
        val metadata = controller.metadata
        val state = controller.playbackState
        val title = metadata
            ?.getString(MediaMetadata.METADATA_KEY_TITLE)
            .orEmpty()
        val artist = metadata
            ?.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: ""
        val playing = state?.state == PlaybackState.STATE_PLAYING
        val duration = metadata
            ?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L
        var position = state?.position ?: 0L
        if (playing && state != null) {
            val elapsed =
                SystemClock.elapsedRealtime() - state.lastPositionUpdateTime
            if (elapsed in 0..10_000) {
                position += (elapsed * state.playbackSpeed).toLong()
            }
        }
        if (duration > 0) {
            position = position.coerceIn(0, duration)
        }
        return Snapshot(title, artist, playing, position, duration)
    }
}
