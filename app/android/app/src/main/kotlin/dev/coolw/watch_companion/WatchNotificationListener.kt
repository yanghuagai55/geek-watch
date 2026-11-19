package dev.coolw.watch_companion

import android.app.Notification
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
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
 * 读取系统媒体会话（汽水音乐、网易云、B 站等）并按分类转发通知到手表。
 *
 * 需要用户在系统里手动开启“通知使用权”，这是 Android 读取其他应用
 * MediaSession 与通知详情的官方入口。
 */
class WatchNotificationListener : NotificationListenerService() {
    companion object {
        private const val FLAG_AUTOGROUP_SUMMARY = 0x00000400

        @Volatile
        var live: WatchNotificationListener? = null

        @Volatile
        private var pendingRescan = false

        @Volatile
        private var pendingIgnoreDismissed = false

        @Volatile
        private var lastRebindAt = 0L

        @Volatile
        private var lastToggleAt = 0L

        private val toggleLock = Any()

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

        fun ensureBound(context: Context, forceToggle: Boolean = false) {
            if (live != null) return
            val appCtx = context.applicationContext
            if (!hasNotificationAccess(appCtx)) return
            val component = ComponentName(appCtx, WatchNotificationListener::class.java)
            val now = SystemClock.uptimeMillis()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
                (forceToggle || now - lastRebindAt >= 2_500L)
            ) {
                lastRebindAt = now
                try {
                    requestRebind(component)
                    Log.i("WatchLink", "已请求系统重新绑定通知监听服务")
                } catch (error: Exception) {
                    Log.w("WatchLink", "请求绑定通知监听失败：${error.message}")
                }
            }
            // Android/HyperOS 在划卡强停（force-stop）后解绑监听但不会加入 snoozed，
            // 仅调 requestRebind() 会被系统忽略；切换自身组件启用态可触发系统重新绑定。
            val canToggle = synchronized(toggleLock) {
                if (live != null) {
                    false
                } else if (forceToggle || lastToggleAt == 0L || now - lastToggleAt >= 30_000L) {
                    lastToggleAt = now
                    true
                } else {
                    false
                }
            }
            if (canToggle) {
                try {
                    val pm = appCtx.packageManager
                    pm.setComponentEnabledSetting(
                        component,
                        PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                        PackageManager.DONT_KILL_APP,
                    )
                    pm.setComponentEnabledSetting(
                        component,
                        PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
                        PackageManager.DONT_KILL_APP,
                    )
                    Log.i("WatchLink", "已触发通知监听组件重建绑定（防强停后系统不回绑）")
                } catch (error: Exception) {
                    Log.w("WatchLink", "通知监听组件重建绑定失败：${error.message}")
                }
            }
        }

        fun rescan(ignoreDismissed: Boolean = false) {
            val activeListener = live
            if (activeListener != null) {
                pendingRescan = false
                pendingIgnoreDismissed = false
                activeListener.seedActive(ignoreDismissed)
            } else {
                pendingRescan = true
                if (ignoreDismissed) pendingIgnoreDismissed = true
                Log.i("WatchLink", "通知监听服务未绑定，等待绑定完成后重扫")
                WatchLinkService.instance?.pushPersistedIfConnected()
            }
        }

        /** 抓取当前通知栏快照（key -> fingerprint），供清空后防回流比对使用。 */
        fun captureActiveSnapshots(): Map<String, String> =
            live?.collectActiveSnapshots().orEmpty()
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
        val force = pendingIgnoreDismissed
        pendingRescan = false
        pendingIgnoreDismissed = false
        seedActive(ignoreDismissed = force)
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

    private fun effectivePackage(sbn: StatusBarNotification): String {
        val pkg = sbn.packageName
        if (pkg == "com.xiaomi.xmsf") {
            val extras = sbn.notification?.extras
            val target = firstNonBlank(
                extras?.getString("xmsf_target_package"),
                extras?.getString("target_package"),
            )
            if (target.isNotEmpty()) return target
        }
        return pkg
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
            val pub = payload.publicVersion
            if (pub != null) return extractTitleText(pub)
            val ticker = payload.tickerText?.toString()?.trim().orEmpty()
            return "" to ticker
        }

        val conversationTitle = firstNonBlank(
            extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE),
        )
        var title = firstNonBlank(
            extras.getCharSequence(Notification.EXTRA_TITLE),
            conversationTitle,
            extras.getCharSequence(Notification.EXTRA_TITLE_BIG),
        )
        var text = firstNonBlank(
            extras.getCharSequence(Notification.EXTRA_BIG_TEXT),
            extras.getCharSequence(Notification.EXTRA_TEXT),
        )

        // MessagingStyle：优先读取最新一条聊天消息（群聊保留群名+发送人）。
        val (msgSender, msgText) = extractLatestMessaging(extras)
        if (msgText.isNotBlank()) {
            if (conversationTitle.isNotBlank()) {
                title = conversationTitle
                text = if (msgSender.isNotBlank() && msgSender != conversationTitle) {
                    "$msgSender: $msgText"
                } else {
                    msgText
                }
            } else {
                if (title.isBlank() && msgSender.isNotBlank()) {
                    title = msgSender
                }
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
            val pub = payload.publicVersion
            if (pub != null && pub !== payload) {
                return extractTitleText(pub)
            }
            text = payload.tickerText?.toString()?.trim().orEmpty()
        }
        return title to text
    }

    private fun readRanking(key: String, rankingMap: RankingMap?): Ranking? {
        val out = Ranking()
        if (rankingMap != null && rankingMap.getRanking(key, out)) return out
        return try {
            if (currentRanking?.getRanking(key, out) == true) out else null
        } catch (_: Exception) {
            null
        }
    }

    private fun buildSignal(
        sbn: StatusBarNotification,
        effectivePkg: String,
        title: String,
        text: String,
        ranking: Ranking?,
    ): NotificationSignal {
        val payload = sbn.notification
        val extras = payload.extras
        val chId = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ranking?.channel?.id?.takeIf { it.isNotBlank() } ?: payload.channelId.orEmpty()
        } else {
            ""
        }
        val chName = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ranking?.channel?.name?.toString()?.trim().orEmpty()
        } else {
            ""
        }
        val isConversation = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ranking?.isConversation == true
        } else {
            false
        }
        val template = extras?.getString(Notification.EXTRA_TEMPLATE).orEmpty()
        @Suppress("DEPRECATION")
        val hasMessages = extras?.containsKey(Notification.EXTRA_MESSAGES) == true
        return NotificationSignal(
            pkg = effectivePkg,
            appName = appLabel(effectivePkg),
            androidCategory = payload.category,
            channelId = chId,
            channelName = chName,
            template = template,
            title = title,
            text = text,
            isConversation = isConversation,
            hasMessages = hasMessages,
        )
    }

    private fun safeActiveNotifications(): Array<StatusBarNotification>? = try {
        activeNotifications
    } catch (_: Exception) {
        null
    }

    override fun onNotificationPosted(
        sbn: StatusBarNotification?,
        rankingMap: RankingMap?,
    ) {
        val notification = sbn ?: return
        val payload = notification.notification ?: return
        val effectivePkg = effectivePackage(notification)
        val (title, text) = extractTitleText(payload)
        val ranking = readRanking(notification.key, rankingMap)
        val activeList = safeActiveNotifications()
        if (!acceptNotification(notification, effectivePkg, title, text, ranking, activeList)) {
            return
        }

        val fp = NotificationRules.fingerprint(notification.postTime, title, text)
        if (NotificationRules.isDismissed(applicationContext, notification.key, fp)) {
            return
        }

        val key = "${notification.key}\u0000$title\u0000$text"
        val now = System.currentTimeMillis()
        if (key == lastNotificationKey && now - lastNotificationAt < 10_000) {
            return
        }
        if (cache(notification, effectivePkg, title, text, ranking)) {
            lastNotificationKey = key
            lastNotificationAt = now
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        val removed = sbn ?: return
        NotificationRules.clearDismissedKey(applicationContext, removed.key)
        val service = WatchLinkService.instance
        if (service != null) {
            service.removeNotification(removed.key)
        } else {
            WatchLinkService.removeOfflineNotification(applicationContext, removed.key)
        }
    }

    private fun collectActiveSnapshots(): Map<String, String> {
        val active = safeActiveNotifications() ?: return emptyMap()
        val out = LinkedHashMap<String, String>(active.size)
        for (sbn in active) {
            val payload = sbn.notification ?: continue
            val (title, text) = extractTitleText(payload)
            if (title.isBlank() && text.isBlank()) continue
            out[sbn.key] = NotificationRules.fingerprint(sbn.postTime, title, text)
        }
        return out
    }

    /** 把通知栏里当前还能看到的消息一次性交给前台服务。 */
    private fun seedActive(ignoreDismissed: Boolean = false) {
        NotificationRules.load(applicationContext)
        if (ignoreDismissed) {
            NotificationRules.clearAllDismissed(applicationContext)
        }
        val active = safeActiveNotifications() ?: return
        val rankingMap = try {
            currentRanking
        } catch (_: Exception) {
            null
        }
        val incoming = active
            .sortedByDescending { it.postTime }
            .mapNotNull { notification ->
                val payload = notification.notification ?: return@mapNotNull null
                val effectivePkg = effectivePackage(notification)
                val (title, text) = extractTitleText(payload)
                val ranking = readRanking(notification.key, rankingMap)
                if (!acceptNotification(notification, effectivePkg, title, text, ranking, active)) {
                    return@mapNotNull null
                }
                val fp = NotificationRules.fingerprint(notification.postTime, title, text)
                if (!ignoreDismissed &&
                    NotificationRules.isDismissed(applicationContext, notification.key, fp)
                ) {
                    return@mapNotNull null
                }
                val signal = buildSignal(notification, effectivePkg, title, text, ranking)
                val classified = NotificationClassifier.classify(signal)
                NotificationRules.rememberApp(this, effectivePkg, classified.appLabel)
                if (NotificationRules.filterPromo && classified.category == WatchCategory.PROMO) {
                    return@mapNotNull null
                }
                if (!NotificationRules.allows(effectivePkg)) return@mapNotNull null
                WatchLinkService.IncomingMessage(
                    app = classified.appLabel,
                    title = title,
                    text = text,
                    key = notification.key,
                    pkg = effectivePkg,
                    categoryId = classified.category.id,
                    categoryLabel = classified.category.label,
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
        effectivePkg: String,
        title: String,
        text: String,
        ranking: Ranking? = null,
        activeList: Array<StatusBarNotification>? = null,
    ): Boolean {
        if (effectivePkg == packageName) return false
        if (effectivePkg in NotificationClassifier.ignoredPackages) return false
        val payload = notification.notification ?: return false
        val extras = payload.extras
        val template = extras?.getString(Notification.EXTRA_TEMPLATE).orEmpty()

        // 1. 媒体播放进度条交由 MediaBridge 处理，不进入通知列表
        if (payload.category == Notification.CATEGORY_TRANSPORT ||
            template.endsWith("MediaStyle") ||
            extras?.containsKey(Notification.EXTRA_MEDIA_SESSION) == true
        ) {
            return false
        }

        // 2. 前台常驻与持续进行中的系统状态（如 USB 调试、充电、后台保活），仅放行来电/闹钟/导航
        val importantOngoingCat = payload.category == Notification.CATEGORY_CALL ||
            payload.category == Notification.CATEGORY_ALARM ||
            payload.category == Notification.CATEGORY_NAVIGATION
        if ((payload.flags and Notification.FLAG_FOREGROUND_SERVICE) != 0 &&
            payload.category != Notification.CATEGORY_CALL &&
            payload.category != Notification.CATEGORY_ALARM
        ) {
            return false
        }
        if ((payload.flags and Notification.FLAG_ONGOING_EVENT) != 0 && !importantOngoingCat) {
            return false
        }

        // 3. HyperOS / MIUI 折叠的不重要通知与纯下载进度条
        if (extras?.getBoolean("miui_unimportant", false) == true) {
            return false
        }
        val progressMax = extras?.getInt(Notification.EXTRA_PROGRESS_MAX, 0) ?: 0
        val progressIndet = extras?.getBoolean(Notification.EXTRA_PROGRESS_INDETERMINATE, false) == true
        if (payload.category == Notification.CATEGORY_PROGRESS ||
            ((progressMax > 0 || progressIndet) &&
                payload.category != Notification.CATEGORY_CALL &&
                payload.category != Notification.CATEGORY_MESSAGE)
        ) {
            return false
        }

        // 4. 分组聚合摘要（存在子通知或属于系统自动分组聚合）
        if ((payload.flags and FLAG_AUTOGROUP_SUMMARY) != 0 ||
            notification.groupKey?.contains("Aggregate_") == true
        ) {
            return false
        }
        if ((payload.flags and Notification.FLAG_GROUP_SUMMARY) != 0) {
            val siblings = activeList
            val hasChild = siblings != null && siblings.any { other ->
                other.key != notification.key &&
                    effectivePackage(other) == effectivePkg &&
                    other.groupKey == notification.groupKey &&
                    ((other.notification?.flags ?: 0) and Notification.FLAG_GROUP_SUMMARY) == 0 &&
                    other.notification?.let { childPayload ->
                        val (ct, cx) = extractTitleText(childPayload)
                        ct.isNotBlank() || cx.isNotBlank()
                    } == true
            }
            if (hasChild) return false
        }

        // 5. 最低重要度静音通知（非来电/闹钟）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
            ranking != null &&
            ranking.importance <= NotificationManager.IMPORTANCE_MIN &&
            !importantOngoingCat
        ) {
            return false
        }

        if (title.isBlank() && text.isBlank()) return false
        return true
    }

    private fun cache(
        notification: StatusBarNotification,
        effectivePkg: String,
        title: String,
        text: String,
        ranking: Ranking?,
    ): Boolean {
        val payload = notification.notification ?: return false
        val signal = buildSignal(notification, effectivePkg, title, text, ranking)
        val classified = NotificationClassifier.classify(signal)
        val category = classified.category
        val appName = classified.appLabel
        NotificationRules.load(applicationContext)
        // 即使当前规则不放行也先记住应用，用户才能在列表里勾选它。
        NotificationRules.rememberApp(this, effectivePkg, appName)
        if (NotificationRules.filterPromo && category == WatchCategory.PROMO) {
            Log.d("WatchLink", "跳过推广通知 [$appName]：$title")
            return false
        }
        if (!NotificationRules.allows(effectivePkg)) return false
        Log.i("WatchLink", "缓存通知 [${category.label}] $appName：$title $text")
        val isSummary = (payload.flags and Notification.FLAG_GROUP_SUMMARY) != 0
        // 只进缓存，手表打开消息页时再按需取；不再走 0x02 文字槽（小字库会出方框）。
        val service = WatchLinkService.instance
        return if (service != null) {
            service.cacheNotification(
                app = appName,
                title = title,
                text = text,
                key = notification.key,
                pkg = effectivePkg,
                categoryId = category.id,
                categoryLabel = category.label,
                isGroupSummary = isSummary,
                groupKey = notification.groupKey,
            )
            true
        } else {
            WatchLinkService.cacheOfflineNotification(
                applicationContext,
                app = appName,
                title = title,
                text = text,
                key = notification.key,
                pkg = effectivePkg,
                categoryId = category.id,
                categoryLabel = category.label,
                isGroupSummary = isSummary,
                groupKey = notification.groupKey,
            )
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
