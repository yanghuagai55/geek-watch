// AT32 手表助手 · GPL-3.0-or-later
//
// 传输层抽象：SPP / BLE / 串口各自实现，上层只依赖这里。
// 本文件不依赖 Flutter，便于命令行探针与单测复用。

import 'dart:typed_data';

/// 三条链路共用的帧协议，[TransportType] 只影响建链方式。
enum TransportType { spp, ble, serial }

enum LinkState { idle, connecting, connected, failed }

class TransportException implements Exception {
  const TransportException(this.message);

  final String message;

  @override
  String toString() => message;
}

abstract interface class WatchTransport {
  /// 用于日志/UI 的链路名称。
  String get displayName;

  bool get isConnected;

  /// 已收到的原始字节（未拆帧）。
  Stream<Uint8List> get incoming;

  /// 链路被对端或系统断开时触发一次。
  Stream<void> get closed;

  /// 建链；失败抛 [TransportException]。
  Future<void> connect();

  Future<void> send(Uint8List bytes);

  Future<void> close();
}
