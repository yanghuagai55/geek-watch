package dev.coolw.watch_companion

import android.app.Notification
import android.content.ComponentName
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log

/**
 * 读取系统媒体会话（汽水音乐、网易云、B 站等）并转发通知。
 *
 * 需要用户在系统里手动开启“通知使用权”，这是 Android 读取其他应用
 * MediaSession 的唯一官方入口；MediaSessionManager.getActiveSessions
 * 也依赖这个组件。
 */
class WatchNotificationListener : NotificationListenerService() {
    private companion object {
        /** 系统状态与广告推送，不进手表消息列表。 */
        val ignoredPackages = setOf(
            "android",
            "com.android.shell",
            "com.android.systemui",
            "com.miui.systemAdSolution",
            "com.miui.securitycenter",
            "com.xiaomi.market",
            "com.xiaomi.shop",
        )
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
        try {
            getSystemService(MediaSessionManager::class.java)
                ?.addOnActiveSessionsChangedListener(sessionsListener, component)
        } catch (_: Exception) {
        }
        refreshSessions()
        seedActive()
    }

    override fun onListenerDisconnected() {
        try {
            getSystemService(MediaSessionManager::class.java)
                ?.removeOnActiveSessionsChangedListener(sessionsListener)
        } catch (_: Exception) {
        }
        super.onListenerDisconnected()
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
        val metadata = active.metadata
        val title = metadata
            ?.getString(MediaMetadata.METADATA_KEY_TITLE)
            .orEmpty()
        val artist = metadata
            ?.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: ""
        val playing =
            active.playbackState?.state == PlaybackState.STATE_PLAYING
        MediaBridge.current = MediaBridge.Snapshot(title, artist, playing)
        WatchLinkService.instance?.onMediaChanged(title, artist, playing)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        val notification = sbn ?: return
        val payload = notification.notification ?: return
        val extras = payload.extras ?: return
        val title = extras
            .getCharSequence(Notification.EXTRA_TITLE)
            ?.toString()
            .orEmpty()
        val text = (
            extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_TEXT)
            )?.toString().orEmpty()

        val key = "${notification.key}\u0000$title\u0000$text"
        val now = System.currentTimeMillis()
        if (key == lastNotificationKey && now - lastNotificationAt < 10_000) {
            return
        }
        lastNotificationKey = key
        lastNotificationAt = now
        cache(notification, title, text)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        sbn?.let { WatchLinkService.instance?.removeNotification(it.key) }
    }

    /** App 重启/监听重连时把通知栏里已有的消息补进缓存。 */
    private fun seedActive() {
        val active = try {
            activeNotifications
        } catch (_: Exception) {
            null
        } ?: return
        active
            .sortedBy { it.postTime }
            .forEach { notification ->
                val payload = notification.notification ?: return@forEach
                val extras = payload.extras ?: return@forEach
                val title = extras
                    .getCharSequence(Notification.EXTRA_TITLE)
                    ?.toString()
                    .orEmpty()
                val text = (
                    extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
                        ?: extras.getCharSequence(Notification.EXTRA_TEXT)
                    )?.toString().orEmpty()
                cache(notification, title, text)
            }
    }

    private fun cache(
        notification: StatusBarNotification,
        title: String,
        text: String,
    ) {
        if (notification.packageName == packageName) return
        if (notification.packageName in ignoredPackages) return
        // 常驻状态（USB 充电、调试、前台服务）不是消息。
        if (notification.isOngoing) return
        val payload = notification.notification ?: return
        if (payload.flags and Notification.FLAG_ONGOING_EVENT != 0) return
        if (payload.flags and Notification.FLAG_FOREGROUND_SERVICE != 0) return
        if (payload.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        val category = payload.category
        if (category == Notification.CATEGORY_TRANSPORT ||
            category == Notification.CATEGORY_PROGRESS
        ) {
            return
        }
        if (title.isBlank() && text.isBlank()) return

        val app = appLabel(notification.packageName)
        Log.d("WatchLink", "缓存通知 $app：$title $text")
        // 只进缓存，手表打开消息页时再按需取；不再走 0x02 文字槽（小字库会出方框）。
        WatchLinkService.instance?.cacheNotification(
            app,
            title,
            text,
            notification.key,
        )
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
    )

    @Volatile
    var current: Snapshot = Snapshot("", "", false)

    @Volatile
    var sessionController: MediaController? = null

    /** 手表 0x10：1 播放 2 暂停 3 下一首 4 上一首。 */
    fun control(action: Int) {
        val controls = sessionController?.transportControls ?: return
        Log.d("WatchLink", "手表控制媒体 action=$action")
        when (action) {
            1 -> controls.play()
            2 -> controls.pause()
            3 -> controls.skipToNext()
            4 -> controls.skipToPrevious()
        }
    }
}
