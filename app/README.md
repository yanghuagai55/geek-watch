# AT32 Watch Companion

Flutter 手表配套界面，支持 Windows 与 Android。当前阶段仅 UI 预览，设备传输、消息发送、歌词同步和反向播放控制尚未接入。

> 要把界面接上真机（HTTP/蓝牙/串口/协议帧），先看
> [BACKEND_HANDOFF.md](BACKEND_HANDOFF.md)：包含待接入清单、建议的 service 分层、
> 协议速查表和已踩过的坑。

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

- 中文：思源宋体 SemiBold（`AT32 Serif SC`），按界面实际用字子集化后打包。
- 英文与数字：Playfair Display 400 / 600 / 700（`AT32 Playfair`）。

两套字体都按 OFL 要求改名（原字体带 Reserved Font Name），许可证见
[`assets/fonts`](assets/fonts)，说明与再生成方法见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

新增中文文案后如果出现豆腐块，把新字加进 `assets/fonts/subset_chars.txt`
再重新子集化即可（具体命令见 THIRD_PARTY_NOTICES）。

## License

This application is licensed under **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The portrait navigation dock and Liquid Glass rendering are derived from
FlClash's `prism_ui` package. Upstream revisions, copyright notices, retained
licenses, and local modifications are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[packages/prism_ui/NOTICE](packages/prism_ui/NOTICE).

If you distribute this application, provide the complete corresponding source
for this `app/` directory under the same license.
