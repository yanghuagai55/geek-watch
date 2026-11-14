package dev.coolw.watch_companion

import android.Manifest
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
    }

    private var events: EventChannel.EventSink? = null
    private var pendingPermission: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

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
                    "isConnected" -> result.success(WatchLinkService.isConnected)
                    "setMediaFollow" -> {
                        val enabled = call.argument<Boolean>("enabled") ?: true
                        WatchLinkService.instance?.setMediaFollow(enabled)
                        result.success(true)
                    }
                    "setNotificationForward" -> {
                        val enabled = call.argument<Boolean>("enabled") ?: true
                        WatchLinkService.instance?.setNotificationForward(enabled)
                        result.success(true)
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
                    else -> result.notImplemented()
                }
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
        WatchLinkService.pendingConnect = { ok, message ->
            if (!ok && !message.isNullOrBlank()) {
                WatchLinkService.emitState(false, message)
            }
            result.success(ok)
        }
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

    private fun hasNotificationAccess(): Boolean {
        val enabled = Settings.Secure.getString(
            contentResolver,
            "enabled_notification_listeners",
        ) ?: return false
        return enabled.split(':').any { entry ->
            val component = android.content.ComponentName.unflattenFromString(entry)
            component?.packageName == packageName
        }
    }

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

    override fun onDestroy() {
        // 只摘掉 UI 的事件通道，前台服务继续持有链路。
        WatchLinkService.attachSink(null)
        events = null
        super.onDestroy()
    }
}
