# AT32 Watch Companion

Flutter 手表配套界面，支持 Windows 与 Android。Windows 端已通过经典 SPP（裸 RFCOMM）接入真机：性能遥测、消息、歌词、亮度、播放态与抓屏均可用；Android 端同样走经典 SPP（需先在系统蓝牙里配对 HC-04，PIN 1234），蓝牙链路由前台服务持有，App 退出后仍保持连接。BLE / 串口尚未接入。

另外已接入 **系统媒体跟随**：Windows 走 SMTC，Android 走 MediaSessionManager（需开启通知使用权），自动读取当前播放的标题/艺人/播放状态并显示到手表音乐页；长标题分页轮播，Windows 端存在同名 `.lrc` 时按时间轴推送歌词。手表上的播放/暂停/上下首会反向控制当前媒体。Android 端还会缓存最近 5 条应用通知：手表打开一级消息页才下发应用名列表，选中后按需加载整屏详情，避免小字库缺字出方框；设置里提供通知读取权限与电池优化白名单入口。

> 要把界面接上真机（HTTP/蓝牙/串口/协议帧），先看
> [BACKEND_HANDOFF.md](BACKEND_HANDOFF.md)：包含已实现架构、协议速查表和已踩过的坑。
> 不启 UI 直连真机自检：`dart run tool/spp_probe.dart`。

## Run

```bat
flutter pub get
flutter run -d windows
```

Windows Release：

```bat
flutter build windows --release
```

产物：`build/windows/x64/runner/Release/watch_companion.exe`。

## 闪退取证

Windows 版内置了两条崩溃记录，都会写到 exe 同目录（不可写时 Dart 侧退到 `%TEMP%`）：

| 文件 | 来源 | 内容 |
| --- | --- | --- |
| `crash.log` | Dart 侧 `runZonedGuarded` / `FlutterError` / `PlatformDispatcher` | 异常文本 + 完整堆栈 |
| `native_crash.log` | `SetUnhandledExceptionFilter` | 异常码、出错地址（模块名 + 偏移）、读写越界地址 |
| `crash_<pid>_<tick>.dmp` | 同一过滤器 | `MiniDumpWriteDump` 生成的 minidump |

遇到闪退后把这三个文件一起发出来即可定位。

自检开关（仅用于验证取证链路，正常启动不会触发）：

```bat
watch_companion.exe --crash-test                            :: 原生侧人为触发访问越界
flutter build windows --release --dart-define=CRASH_TEST=true :: Dart 侧人为抛异常
```

## 字体

- 中文：思源宋体 SemiBold（`AT32 Serif SC`），按界面实际用字子集化后打包，当前覆盖全部界面字符串用字（479 个字形）。
- 英文与数字：Playfair Display 400 / 600 / 700（`AT32 Playfair`）；标题与大数字使用它，中文标题自动落回宋体。

两套字体都按 OFL 要求改名（原字体带 Reserved Font Name），许可证见
[`assets/fonts`](assets/fonts)，说明与再生成方法见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

新增中文文案后如果出现豆腐块，说明字形不在子集里。重跑：

```bat
python tool/make_font_subset.py --source <原版 SourceHanSerifSC-SemiBold.otf>
```

脚本会扫描 `lib` 里的 Dart 字符串、重写 `assets/fonts/subset_chars.txt` 并重新子集化，同时按 OFL 要求把内部字体名改成 `AT32 Serif SC`。

## License

This application is licensed under **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The portrait navigation dock and Liquid Glass rendering are derived from
FlClash's `prism_ui` package. Upstream revisions, copyright notices, retained
licenses, and local modifications are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[packages/prism_ui/NOTICE](packages/prism_ui/NOTICE).

If you distribute this application, provide the complete corresponding source
for this `app/` directory under the same license.
