package dev.coolw.watch_companion

import android.content.Context
import android.content.SharedPreferences

/**
 * 通知转发规则：路由模式、允许的应用、最多缓存条数、同应用合并。
 *
 * 规则和前台服务共用一份 SharedPreferences，App 退出后依然生效；
 * Flutter 侧进入消息页时通过 MethodChannel 回读并展示。
 */
object NotificationRules {
    const val MODE_ALL = "all"
    const val MODE_SELECTED = "selected"

    private const val PREFS = "watch_link"
    private const val KEY_MODE = "message_route_mode"
    private const val KEY_PACKAGES = "message_route_packages"
    private const val KEY_MAX = "message_max"
    private const val KEY_MERGE = "message_merge"
    private const val KEY_APPS = "message_seen_apps"

    @Volatile
    var mode: String = MODE_ALL
        private set

    @Volatile
    var packages: Set<String> = emptySet()
        private set

    @Volatile
    var maxMessages: Int = 5
        private set

    @Volatile
    var merge: Boolean = true
        private set

    private var prefs: SharedPreferences? = null

    fun load(context: Context) {
        if (prefs != null) return
        val store = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs = store
        mode = if (store.getString(KEY_MODE, MODE_ALL) == MODE_SELECTED) {
            MODE_SELECTED
        } else {
            MODE_ALL
        }
        packages = store.getStringSet(KEY_PACKAGES, emptySet())?.toSet().orEmpty()
        maxMessages = store.getInt(KEY_MAX, 10).coerceIn(1, 20)
        merge = store.getBoolean(KEY_MERGE, true)
    }

    fun update(
        mode: String,
        packages: Set<String>,
        maxMessages: Int,
        merge: Boolean,
    ) {
        this.mode = if (mode == MODE_SELECTED) MODE_SELECTED else MODE_ALL
        this.packages = packages.toSet()
        this.maxMessages = maxMessages.coerceIn(1, 20)
        this.merge = merge
        prefs?.edit()
            ?.putString(KEY_MODE, this.mode)
            ?.putStringSet(KEY_PACKAGES, this.packages)
            ?.putInt(KEY_MAX, this.maxMessages)
            ?.putBoolean(KEY_MERGE, this.merge)
            ?.apply()
    }

    /** 只有“仅选中应用”模式才按包名过滤。 */
    fun allows(pkg: String): Boolean =
        mode != MODE_SELECTED || pkg in packages

    /** 记录通知栏出现过的应用，供勾选列表使用。 */
    fun rememberApp(context: Context, pkg: String, label: String) {
        val store = prefs ?: context
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .also { prefs = it }
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

    fun apps(context: Context): List<Map<String, String>> {
        val store = prefs
            ?: context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
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
