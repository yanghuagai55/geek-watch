package dev.coolw.watch_companion

import android.app.Notification

/**
 * 手表消息分类定义与规则引擎。
 *
 * 结合真机（HyperOS / Android 17）实测通知字段（packageName、opPkg、
 * Notification.category、NotificationChannel 名称/ID、template 及正文特征）
 * 对通知做精确归类并过滤纯系统状态/常驻噪音。
 */
enum class WatchCategory(
    val id: String,
    val label: String,
    val priority: Int,
) {
    CALL("call", "来电", 100),
    CHAT("chat", "聊天", 90),
    SMS("sms", "短信", 85),
    PAY("pay", "支付", 80),
    ORDER("order", "订单", 75),
    ALARM("alarm", "闹钟", 70),
    SCHEDULE("schedule", "日程", 65),
    NAV("nav", "出行", 60),
    EMAIL("email", "邮件", 55),
    OTHER("other", "通知", 50),
    MEDIA("media", "媒体", 45),
    NEWS("news", "资讯", 40),
    SYSTEM("system", "系统", 30),
    PROMO("promo", "推广", 10),
    ;

    companion object {
        fun fromId(id: String?): WatchCategory =
            entries.firstOrNull { it.id == id } ?: OTHER
    }
}

data class NotificationSignal(
    val pkg: String,
    val appName: String,
    val androidCategory: String? = null,
    val channelId: String = "",
    val channelName: String = "",
    val template: String = "",
    val title: String = "",
    val text: String = "",
    val isConversation: Boolean = false,
    val hasMessages: Boolean = false,
)

data class NotificationClassification(
    val category: WatchCategory,
    val appLabel: String,
)

object NotificationClassifier {
    /** 固定系统壳进程/广告组件/守护进程：不向手表同步。 */
    val ignoredPackages = setOf(
        "android",
        "com.android.shell",
        "com.android.systemui",
        "com.lbe.security.miui",
        "com.miui.analytics",
        "com.miui.cleanmaster",
        "com.miui.guardprovider",
        "com.miui.powerkeeper",
        "com.miui.securitycenter",
        "com.miui.systemAdSolution",
        "com.xiaomi.market",
        "com.xiaomi.shop",
        "com.xiaomi.xmsf",
        "com.xiaomi.xmsfkeeper",
    )

    private val callPackages = setOf(
        "com.android.dialer",
        "com.android.incallui",
        "com.android.server.telecom",
        "com.google.android.dialer",
        "com.xiaomi.phone",
    )

    private val smsPackages = setOf(
        "com.android.messaging",
        "com.android.mms",
        "com.google.android.apps.messaging",
        "com.xiaomi.mms",
    )

    private val chatPackages = setOf(
        "com.alibaba.android.rimet",
        "com.discord",
        "com.slack",
        "com.ss.android.lark",
        "com.tencent.mm",
        "com.tencent.mobileqq",
        "com.tencent.tim",
        "com.tencent.wework",
        "com.tencent.workbuddy.app",
        "com.whatsapp",
        "org.telegram.messenger",
    )

    private val feedPackages = setOf(
        "com.baidu.searchbox",
        "com.baidu.tieba",
        "com.hexin.plat.android",
        "com.microsoft.emmx",
        "com.quark.browser",
        "com.sina.weibo",
        "com.smile.gifmaker",
        "com.ss.android.article.news",
        "com.ss.android.ugc.aweme",
        "com.xingin.xhs",
        "com.zhihu.android",
        "tv.danmaku.bili",
    )

    private val payPackages = setOf(
        "cmb.pb",
        "com.android.bankabc",
        "com.antfortune.wealth",
        "com.ccb.android",
        "com.chinamworld.bocmbci",
        "com.eg.android.AlipayGphone",
        "com.icbc",
        "com.mipay.wallet",
        "com.unionpay",
    )

    private val orderPackages = setOf(
        "com.MobileTicket",
        "com.cainiao.wireless",
        "com.jingdong.app.mall",
        "com.mcdonalds.gma.cn",
        "com.sankuai.meituan",
        "com.sf.activity",
        "com.szjlc.mobile",
        "com.taobao.idlefish",
        "com.taobao.taobao",
        "com.xunmeng.pinduoduo",
        "me.ele",
    )

    private val alarmPackages = setOf(
        "com.android.deskclock",
        "com.google.android.deskclock",
    )

    private val schedulePackages = setOf(
        "com.android.calendar",
        "com.miui.notes",
        "com.suda.yzune.wakeupschedule",
        "com.tencent.wemeet.app",
        "com.xiaomi.calendar",
    )

    private val navPackages = setOf(
        "com.autonavi.minimap",
        "com.baidu.BaiduMap",
        "com.google.android.apps.maps",
        "com.jingyao.easybike",
        "com.sdu.didi.psnger",
        "com.taobao.trip",
        "com.tongcheng.android",
    )

    private val emailPackages = setOf(
        "com.android.email",
        "com.google.android.gm",
        "com.kingsoft.email",
        "com.microsoft.office.outlook",
        "com.netease.mail",
        "com.tencent.androidqqmail",
    )

    private val mediaPackages = setOf(
        "com.kugou.android",
        "com.luna.music",
        "com.miui.player",
        "com.music.android.goat",
        "com.netease.cloudmusic",
        "com.tencent.qqmusic",
    )

    private val callKeywords = listOf(
        "语音通话",
        "视频通话",
        "来电",
        "未接电话",
        "未接来电",
        "邀请你语音",
        "邀请你视频",
        "邀请你加入会议",
        "正在通话",
    )

    private val promoChannelKeywords = listOf(
        "业务活动",
        "营销",
        "推广",
        "广告",
        "优惠",
        "福利",
        "活动通知",
        "活动推送",
        "精选推荐",
    )

    private val promoTextKeywords = listOf(
        "0元领",
        "免费领",
        "领会员",
        "限时特惠",
        "点击领取",
        "速点>>>",
        "优惠券",
        "满减券",
        "特价抢购",
        "不领失效",
        "立减金待领",
        "现金红包待领",
    )

    private val payTitlePrefixes = listOf(
        "微信支付",
        "支付宝",
        "云闪付",
        "收款通知",
        "动账提醒",
    )

    private val payKeywords = listOf(
        "已支付",
        "付款成功",
        "支付成功",
        "交易成功",
        "收款",
        "到账",
        "转入",
        "转出",
        "扣款",
        "消费¥",
        "消费￥",
        "余额变动",
        "入账",
        "出账",
        "还款成功",
        "收益更新",
        "微银行",
    )

    private val orderChannelKeywords = listOf(
        "订单",
        "物流",
        "配送",
        "快递",
        "外卖",
        "取餐",
    )

    private val orderKeywords = listOf(
        "取件码",
        "取餐码",
        "已备好",
        "上门自取",
        "驿站",
        "快递柜",
        "丰巢",
        "菜鸟",
        "已送达",
        "派送中",
        "已发货",
        "骑手",
        "出票成功",
        "检票",
        "候补成功",
        "商品已准备好",
    )

    private val chatInteractionKeywords = listOf(
        "私信",
        "回复了你",
        "评论了你",
        "@了你",
        "提到了你",
        "发来消息",
        "发来一条消息",
    )

    private val newsChannelKeywords = listOf(
        "热搜",
        "资讯",
        "新闻",
        "订阅",
        "直播",
        "视频",
    )

    fun classify(signal: NotificationSignal): NotificationClassification {
        val pkg = signal.pkg.lowercase()
        val chName = signal.channelName.lowercase()
        val chId = signal.channelId.lowercase()
        val title = signal.title.trim()
        val text = signal.text.trim()
        val combined = "$title $text"
        val combinedLower = combined.lowercase()
        val cat = signal.androidCategory

        // 1. 来电 / 通话（最高优先级）
        if (cat == Notification.CATEGORY_CALL ||
            cat == Notification.CATEGORY_MISSED_CALL ||
            cat == Notification.CATEGORY_VOICEMAIL ||
            signal.template.endsWith("CallStyle") ||
            pkg in callPackages ||
            ((pkg in chatPackages || pkg == "com.tencent.wemeet.app") &&
                containsAny(combined, callKeywords))
        ) {
            return NotificationClassification(WatchCategory.CALL, signal.appName)
        }

        // 2. 强特征交易/账单（避免在推广前被误判，也避免如“微信支付”被归进普通聊天）
        val isPayContent = payTitlePrefixes.any { title.startsWith(it) } ||
            containsAny(combined, payKeywords)

        // 3. 推广营销（如 HyperOS 小米钱包 channelName="业务活动" 的 0 元领会员）
        if (!isPayContent &&
            (cat == Notification.CATEGORY_PROMO ||
                cat == Notification.CATEGORY_RECOMMENDATION ||
                containsAny(chName, promoChannelKeywords) ||
                containsAny(chId, promoChannelKeywords) ||
                containsAny(combinedLower, promoTextKeywords))
        ) {
            return NotificationClassification(WatchCategory.PROMO, signal.appName)
        }

        // 4. 短信（保留为独立“短信”分类，便于识别验证码/运营商/系统短信）
        if (pkg in smsPackages || pkg.contains("mms")) {
            return NotificationClassification(WatchCategory.SMS, signal.appName)
        }

        // 5. 支付与金融账单
        if (isPayContent ||
            pkg in payPackages ||
            pkg.contains("bank") ||
            pkg.contains("wallet") ||
            pkg.contains("alipay") ||
            pkg.startsWith("com.csii.")
        ) {
            val refinedApp = when {
                pkg == "com.tencent.mm" && title == "微信支付" -> "微信支付"
                else -> signal.appName
            }
            return NotificationClassification(WatchCategory.PAY, refinedApp)
        }

        // 6. 订单与物流取件（如美团 channelName="订单通知"、快递取件码）
        if (containsAny(chName, orderChannelKeywords) ||
            containsAny(combined, orderKeywords) ||
            (pkg in orderPackages && containsAny(combined, listOf("订单", "备好", "自取", "配送", "送达", "发货", "退款")))
        ) {
            return NotificationClassification(WatchCategory.ORDER, signal.appName)
        }

        // 7. 闹钟与计时
        if (cat == Notification.CATEGORY_ALARM ||
            cat == Notification.CATEGORY_STOPWATCH ||
            pkg in alarmPackages
        ) {
            return NotificationClassification(WatchCategory.ALARM, signal.appName)
        }

        // 8. 日程、课程表与会议提醒
        if (cat == Notification.CATEGORY_EVENT ||
            cat == Notification.CATEGORY_REMINDER ||
            pkg in schedulePackages
        ) {
            return NotificationClassification(WatchCategory.SCHEDULE, signal.appName)
        }

        // 9. 出行与导航
        if (cat == Notification.CATEGORY_NAVIGATION || pkg in navPackages) {
            return NotificationClassification(WatchCategory.NAV, signal.appName)
        }

        // 10. 邮件
        if (cat == Notification.CATEGORY_EMAIL ||
            pkg in emailPackages ||
            pkg.contains("mail")
        ) {
            return NotificationClassification(WatchCategory.EMAIL, signal.appName)
        }

        // 11. 聊天与社交互动（含 B 站/微博/小红书私信与回复）
        if (cat == Notification.CATEGORY_MESSAGE ||
            cat == Notification.CATEGORY_SOCIAL ||
            signal.template.endsWith("MessagingStyle") ||
            signal.hasMessages ||
            signal.isConversation ||
            pkg in chatPackages ||
            (pkg in feedPackages && containsAny(combined, chatInteractionKeywords))
        ) {
            return NotificationClassification(WatchCategory.CHAT, signal.appName)
        }

        // 12. 媒体
        if (cat == Notification.CATEGORY_TRANSPORT || pkg in mediaPackages) {
            return NotificationClassification(WatchCategory.MEDIA, signal.appName)
        }

        // 13. 资讯与订阅推送
        if (pkg in feedPackages ||
            pkg in orderPackages ||
            containsAny(chName, newsChannelKeywords)
        ) {
            return NotificationClassification(WatchCategory.NEWS, signal.appName)
        }

        // 14. 系统通知
        if (cat == Notification.CATEGORY_SYSTEM ||
            cat == Notification.CATEGORY_ERROR ||
            cat == Notification.CATEGORY_STATUS ||
            cat == Notification.CATEGORY_SERVICE ||
            pkg.startsWith("com.android.") ||
            pkg.startsWith("com.miui.") ||
            pkg.startsWith("com.xiaomi.") ||
            pkg.startsWith("com.google.android.") ||
            pkg.startsWith("com.qualcomm.")
        ) {
            return NotificationClassification(WatchCategory.SYSTEM, signal.appName)
        }

        return NotificationClassification(WatchCategory.OTHER, signal.appName)
    }

    private fun containsAny(haystack: String, needles: List<String>): Boolean {
        if (haystack.isEmpty()) return false
        for (needle in needles) {
            if (haystack.contains(needle, ignoreCase = true)) return true
        }
        return false
    }
}
