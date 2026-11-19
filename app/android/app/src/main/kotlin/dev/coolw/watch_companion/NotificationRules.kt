package dev.coolw.watch_companion

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONObject

/**
 * 通知转发规则：路由模式、允许的应用、推广过滤、最多缓存条数、同应用合并，
 * 以及跨重启保留的“已清理基线”（避免手表清空后旧通知回流）。
 */
object NotificationRules {
    const val MODE_ALL = "all"
    const val MODE_SELECTED = "selected"

    private const val PREFS = "watch_link"
    private const val KEY_MODE = "message_route_mode"
    private const val KEY_PACKAGES = "message_route_packages"
    private const val KEY_MAX = "message_max"
    private const val KEY_MERGE = "message_merge"
    private const val KEY_FILTER_PROMO = "message_filter_promo"
    private const val KEY_APPS = "message_seen_apps"
    private const val KEY_NOTIFY = "notification_forward"
    private const val KEY_DISMISSED = "dismissed_notifications"
    private const val MAX_DISMISSED = 200

    @Volatile
    var mode: String = MODE_ALL
        private set

    @Volatile
    var packages: Set<String> = emptySet()
        private set

    @Volatile
    var maxMessages: Int = 10
        private set

    @Volatile
    var merge: Boolean = true
        private set

    @Volatile
    var filterPromo: Boolean = true
        private set

    @Volatile
    private var loaded = false

    private var prefs: SharedPreferences? = null
    private val dismissed = LinkedHashMap<String, String>()
    private var dismissedLoaded = false

    private fun store(context: Context): SharedPreferences =
        prefs ?: context.applicationContext
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .also { prefs = it }

    @Synchronized
    fun load(context: Context) {
        val store = store(context)
        if (!dismissedLoaded) {
            dismissedLoaded = true
            val raw = store.getString(KEY_DISMISSED, null)
            if (!raw.isNullOrEmpty()) {
                try {
                    val obj = JSONObject(raw)
                    val keys = obj.keys()
                    while (keys.hasNext()) {
                        val k = keys.next()
                        dismissed[k] = obj.optString(k)
                    }
                } catch (_: Exception) {
                }
            }
        }
        if (loaded) return
        loaded = true
        mode = if (store.getString(KEY_MODE, MODE_ALL) == MODE_SELECTED) {
            MODE_SELECTED
        } else {
            MODE_ALL
        }
        packages = store.getStringSet(KEY_PACKAGES, emptySet())?.toSet().orEmpty()
        maxMessages = store.getInt(KEY_MAX, 10).coerceIn(1, 20)
        merge = store.getBoolean(KEY_MERGE, true)
        filterPromo = store.getBoolean(KEY_FILTER_PROMO, true)
    }

    @Synchronized
    fun update(
        mode: String,
        packages: Set<String>,
        maxMessages: Int,
        merge: Boolean,
        filterPromo: Boolean = this.filterPromo,
    ) {
        loaded = true
        this.mode = if (mode == MODE_SELECTED) MODE_SELECTED else MODE_ALL
        this.packages = packages.toSet()
        this.maxMessages = maxMessages.coerceIn(1, 20)
        this.merge = merge
        this.filterPromo = filterPromo
        prefs?.edit()
            ?.putString(KEY_MODE, this.mode)
            ?.putStringSet(KEY_PACKAGES, this.packages)
            ?.putInt(KEY_MAX, this.maxMessages)
            ?.putBoolean(KEY_MERGE, this.merge)
            ?.putBoolean(KEY_FILTER_PROMO, this.filterPromo)
            ?.apply()
    }

    /** 只有“仅选中应用”模式才按包名过滤。 */
    @Synchronized
    fun allows(pkg: String): Boolean =
        mode != MODE_SELECTED || pkg in packages

    fun isNotificationForwardEnabled(context: Context): Boolean =
        store(context).getBoolean(KEY_NOTIFY, true)

    fun fingerprint(postTime: Long, title: String, text: String): String =
        "$postTime\u0000$title\u0000$text"

    /** 检查是否处于“用户已清空”基线中；若内容/时间戳已更新则自动解除。 */
    @Synchronized
    fun isDismissed(context: Context, key: String, fp: String): Boolean {
        load(context)
        val recorded = dismissed[key] ?: return false
        if (recorded == fp) return true
        dismissed.remove(key)
        persistDismissedLocked(store(context))
        return false
    }

    @Synchronized
    fun markDismissedBatch(context: Context, snapshots: Map<String, String>) {
        load(context)
        if (snapshots.isEmpty()) return
        for ((key, fp) in snapshots) {
            dismissed.remove(key)
            dismissed[key] = fp
        }
        while (dismissed.size > MAX_DISMISSED) {
            val oldest = dismissed.keys.firstOrNull() ?: break
            dismissed.remove(oldest)
        }
        persistDismissedLocked(store(context))
    }

    @Synchronized
    fun clearDismissedKey(context: Context, key: String) {
        load(context)
        if (dismissed.remove(key) != null) {
            persistDismissedLocked(store(context))
        }
    }

    @Synchronized
    fun clearAllDismissed(context: Context) {
        load(context)
        if (dismissed.isNotEmpty()) {
            dismissed.clear()
            persistDismissedLocked(store(context))
        }
    }

    private fun persistDismissedLocked(store: SharedPreferences) {
        val obj = JSONObject()
        for ((k, v) in dismissed) {
            obj.put(k, v)
        }
        store.edit().putString(KEY_DISMISSED, obj.toString()).apply()
    }

    /** 记录通知栏出现过的应用，供勾选列表使用。 */
    @Synchronized
    fun rememberApp(context: Context, pkg: String, label: String) {
        load(context)
        val store = store(context)
        val current = store
            .getStringSet(KEY_APPS, emptySet())
            ?.toMutableSet()
            ?: mutableSetOf()
        // 同一个包换了显示名时，先移除旧条目。
        current.removeAll { it.substringBefore('\u0000') == pkg }
        current.add("$pkg\u0000$label")
        while (current.size > 60) {
            current.remove(current.first())
        }
        store.edit().putStringSet(KEY_APPS, current).apply()
    }

    @Synchronized
    fun apps(context: Context): List<Map<String, String>> {
        val store = store(context)
        return store.getStringSet(KEY_APPS, emptySet())
            .orEmpty()
            .mapNotNull { entry ->
                val split = entry.split('\u0000', limit = 2)
                if (split.size != 2 || split[0].isBlank()) {
                    null
                } else {
                    mapOf(
                        "package" to split[0],
                        "label" to split[1].ifBlank { split[0] },
                    )
                }
            }
            .sortedBy { it["label"] }
    }
}
