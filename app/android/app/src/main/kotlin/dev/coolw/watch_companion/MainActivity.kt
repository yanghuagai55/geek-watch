package dev.coolw.watch_companion

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import android.provider.Settings
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

/**
 * Flutter 桥：链路本体在 [WatchLinkService] 里，Activity 退出不会断开手表。
 * 这里只负责权限、前台服务拉起和事件转发。
 */
class MainActivity : FlutterActivity() {
    companion object {
        private const val CHANNEL = "watch_companion/bluetooth"
        private const val EVENTS = "watch_companion/bluetooth/events"
        private const val PERMISSION_REQUEST = 1001
        private const val PICK_IMAGE_REQUEST = 1002
    }

    private var events: EventChannel.EventSink? = null
    private var pendingPermission: MethodChannel.Result? = null
    private var pendingPickImage: MethodChannel.Result? = null
    private var lastNotificationAccess = false

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        NotificationRules.load(applicationContext)
        lastNotificationAccess =
            WatchNotificationListener.hasNotificationAccess(applicationContext)
        WatchNotificationListener.ensureBound(applicationContext)

        EventChannel(flutterEngine.dartExecutor.binaryMessenger, EVENTS)
            .setStreamHandler(object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, sink: EventChannel.EventSink?) {
                    events = sink
                    WatchLinkService.attachSink(sink)
                }

                override fun onCancel(arguments: Any?) {
                    WatchLinkService.attachSink(null)
                    events = null
                }
            })

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "requestPermissions" -> requestPermissions(result)
                    "connect" -> {
                        val address = call.argument<String>("address")
                        if (address.isNullOrBlank()) {
                            result.error("ARG", "蓝牙地址为空", null)
                        } else {
                            connect(address, result)
                        }
                    }
                    "send" -> {
                        val bytes = call.argument<ByteArray>("bytes")
                        val service = WatchLinkService.instance
                        if (bytes == null) {
                            result.error("ARG", "发送数据为空", null)
                        } else if (service == null) {
                            result.success(false)
                        } else {
                            service.send(bytes) { ok -> result.success(ok) }
                        }
                    }
                    "close" -> {
                        WatchLinkService.instance?.shutdown()
                        result.success(true)
                    }
                    "forgetBluetooth" -> {
                        val address = call.argument<String>("address").orEmpty()
                        result.success(forgetBluetooth(address))
                    }
                    "isConnected" -> result.success(WatchLinkService.isConnected)
                    "nowPlaying" -> result.success(
                        MediaBridge.nowPlaying(applicationContext),
                    )
                    "mediaControl" -> {
                        val action = call.argument<String>("action") ?: ""
                        MediaBridge.controlAction(action, applicationContext)
                        result.success(true)
                    }
                    "seekTo" -> {
                        val position = call.argument<Number>("positionMs")
                            ?.toLong() ?: 0L
                        MediaBridge.seekTo(position, applicationContext)
                        result.success(true)
                    }
                    "messageCount" -> result.success(
                        WatchLinkService.cachedMessageCount(applicationContext),
                    )
                    "clearMessages" -> {
                        WatchLinkService.clearMessages(applicationContext)
                        result.success(true)
                    }
                    "recordManualMessage" -> {
                        val app = call.argument<String>("app") ?: ""
                        val title = call.argument<String>("title") ?: ""
                        val text = call.argument<String>("text") ?: ""
                        WatchLinkService.instance?.recordManualMessage(app, title, text)
                        result.success(true)
                    }
                    "messageRules" -> result.success(
                        mapOf(
                            "mode" to NotificationRules.mode,
                            "packages" to NotificationRules.packages.toList(),
                            "maxMessages" to NotificationRules.maxMessages,
                            "merge" to NotificationRules.merge,
                        ),
                    )
                    "setMessageRules" -> {
                        NotificationRules.update(
                            mode = call.argument<String>("mode") ?: "all",
                            packages = call
                                .argument<List<String>>("packages")
                                ?.toSet()
                                ?: emptySet(),
                            maxMessages = call
                                .argument<Number>("maxMessages")
                                ?.toInt()
                                ?: 5,
                            merge = call.argument<Boolean>("merge") ?: true,
                        )
                        WatchLinkService.instance?.applyMessageRules()
                        android.util.Log.d(
                            "WatchLink",
                            "消息规则 mode=${NotificationRules.mode} " +
                                "apps=${NotificationRules.packages.size} " +
                                "max=${NotificationRules.maxMessages} " +
                                "merge=${NotificationRules.merge}",
                        )
                        result.success(true)
                    }
                    "notificationApps" -> result.success(
                        NotificationRules.apps(applicationContext),
                    )
                    "setMediaFollow" -> {
                        val enabled = call.argument<Boolean>("enabled") ?: true
                        WatchLinkService.setMediaFollow(applicationContext, enabled)
                        result.success(true)
                    }
                    "setNotificationForward" -> {
                        val enabled = call.argument<Boolean>("enabled") ?: true
                        WatchLinkService.setNotificationForward(applicationContext, enabled)
                        result.success(true)
                    }
                    "setDartTransfer" -> {
                        val active = call.argument<Boolean>("active") ?: false
                        WatchLinkService.instance?.setDartTransferActive(active)
                        result.success(true)
                    }
                    "rescanNotifications" -> {
                        WatchNotificationListener.ensureBound(applicationContext)
                        WatchNotificationListener.rescan()
                        result.success(WatchNotificationListener.live != null)
                    }
                    "notificationAccess" -> result.success(hasNotificationAccess())
                    "openNotificationAccess" -> {
                        openNotificationAccess()
                        result.success(true)
                    }
                    "batteryExempt" -> result.success(isBatteryExempt())
                    "openBatterySettings" -> {
                        openBatterySettings()
                        result.success(true)
                    }
                    "pickImage" -> startPickImage(result)
                    else -> result.notImplemented()
                }
            }
    }

    /**
     * 删掉系统里这块手表的配对记录。试用过的手机会一直记住 HC-04，
     * 之后抢着自动连上来；清掉之后必须重新输入 PIN 才能再连。
     */
    private fun forgetBluetooth(address: String): String {
        val adapter = BluetoothAdapter.getDefaultAdapter()
            ?: return "这台设备没有蓝牙"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return "没有蓝牙连接权限，无法清除配对"
        }
        WatchLinkService.instance?.shutdown()
        val wanted = linkedSetOf<String>()
        val typed = address.trim()
        if (typed.isNotEmpty()) wanted.add(typed.uppercase())
        if (typed.startsWith("04:", ignoreCase = true) && typed.length > 3) {
            wanted.add("C4:${typed.substring(3)}".uppercase())
        }
        val bonded = adapter.bondedDevices.orEmpty()
        for (device in bonded) {
            val name = device.name.orEmpty()
            val addr = device.address.uppercase()
            if (wanted.contains(addr) || name.equals("HC-04", ignoreCase = true)) {
                wanted.add(addr)
            }
        }
        if (wanted.isEmpty()) return "没有可清除的手表地址"
        val removed = mutableListOf<String>()
        val failed = mutableListOf<String>()
        for (addr in wanted) {
            val device = try {
                adapter.getRemoteDevice(addr)
            } catch (_: IllegalArgumentException) {
                failed.add(addr)
                continue
            }
            val paired = device.bondState == BluetoothDevice.BOND_BONDED ||
                bonded.any { it.address.equals(addr, ignoreCase = true) }
            if (!paired) continue
            val ok = try {
                val method = device.javaClass.getMethod("removeBond")
                method.invoke(device) as? Boolean ?: false
            } catch (_: Exception) {
                false
            }
            if (ok) removed.add(addr) else failed.add(addr)
        }
        return when {
            removed.isNotEmpty() && failed.isEmpty() ->
                "已清除系统配对：${removed.joinToString("、")}"
            removed.isNotEmpty() ->
                "已清除 ${removed.joinToString("、")}，其余未成功：${failed.joinToString("、")}"
            failed.isNotEmpty() ->
                "系统拒绝删除配对：${failed.joinToString("、")}"
            else -> "系统里没有这块手表的配对记录"
        }
    }

    private fun requestPermissions(result: MethodChannel.Result) {
        val wanted = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            wanted.add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            wanted.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (wanted.isEmpty()) {
            result.success(true)
            return
        }
        pendingPermission = result
        requestPermissions(wanted.toTypedArray(), PERMISSION_REQUEST)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != PERMISSION_REQUEST) return
        var bluetoothGranted = true
        permissions.forEachIndexed { index, permission ->
            if (permission == Manifest.permission.BLUETOOTH_CONNECT) {
                bluetoothGranted =
                    grantResults.getOrNull(index) ==
                    PackageManager.PERMISSION_GRANTED
            }
        }
        pendingPermission?.success(bluetoothGranted)
        pendingPermission = null
    }

    private fun connect(address: String, result: MethodChannel.Result) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            result.error("PERMISSION", "缺少蓝牙连接权限", null)
            return
        }
        WatchLinkService.pendingConnect?.let { previous ->
            WatchLinkService.pendingConnect = null
            previous(false, null)
        }
        WatchLinkService.pendingConnect = { ok, message ->
            if (!ok && !message.isNullOrBlank()) {
                WatchLinkService.emitState(false, message)
            }
            result.success(ok)
        }
        WatchNotificationListener.ensureBound(applicationContext)
        val intent = Intent(this, WatchLinkService::class.java).apply {
            action = WatchLinkService.ACTION_CONNECT
            putExtra(WatchLinkService.EXTRA_ADDRESS, address)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }

    override fun onResume() {
        super.onResume()
        val hasAccess = hasNotificationAccess()
        val newlyGranted = !lastNotificationAccess && hasAccess
        lastNotificationAccess = hasAccess
        WatchNotificationListener.ensureBound(applicationContext)
        if (WatchLinkService.isConnected &&
            (newlyGranted || WatchLinkService.cachedMessageCount(applicationContext) == 0)
        ) {
            WatchNotificationListener.rescan()
        }
    }

    private fun hasNotificationAccess(): Boolean =
        WatchNotificationListener.hasNotificationAccess(applicationContext)

    private fun openNotificationAccess() {
        val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            startActivity(intent)
        } catch (_: Exception) {
            startActivity(
                Intent(Settings.ACTION_SETTINGS)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    private fun isBatteryExempt(): Boolean {
        val power = getSystemService(PowerManager::class.java) ?: return false
        return power.isIgnoringBatteryOptimizations(packageName)
    }

    private fun openBatterySettings() {
        val request = Intent(
            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
            Uri.parse("package:$packageName"),
        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            startActivity(request)
        } catch (_: Exception) {
            startActivity(
                Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    private fun startPickImage(result: MethodChannel.Result) {
        if (pendingPickImage != null) {
            result.error("BUSY", "图片选择窗口已打开", null)
            return
        }
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "image/*"
        }
        pendingPickImage = result
        try {
            startActivityForResult(intent, PICK_IMAGE_REQUEST)
        } catch (_: Exception) {
            pendingPickImage = null
            result.success(null)
        }
    }

    @Suppress("DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_IMAGE_REQUEST) return
        val pending = pendingPickImage
        pendingPickImage = null
        if (pending == null) return
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) {
            pending.success(null)
            return
        }
        try {
            var name = "photo"
            contentResolver.query(
                uri,
                arrayOf(android.provider.OpenableColumns.DISPLAY_NAME),
                null,
                null,
                null,
            )?.use { cursor ->
                if (cursor.moveToFirst()) {
                    name = cursor.getString(0) ?: name
                }
            }
            val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() }
            if (bytes == null || bytes.isEmpty()) {
                pending.success(null)
            } else {
                pending.success(mapOf("name" to name, "bytes" to bytes))
            }
        } catch (e: Exception) {
            pending.error("IO", e.message ?: "读取图片失败", null)
        }
    }

    override fun onDestroy() {
        // 只摘掉 UI 的事件通道，前台服务继续持有链路。
        WatchLinkService.attachSink(null)
        events = null
        super.onDestroy()
    }
}
