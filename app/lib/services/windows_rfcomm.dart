// AT32 手表助手 · GPL-3.0-or-later
//
// Windows 经典 SPP：裸 RFCOMM socket（AF_BTH + BTPROTO_RFCOMM，通道 1）。
// 系统分配的虚拟 COM 口打不开（5 秒超时/卡死），必须走这条路。
//
// 接收是阻塞的，放在独立 isolate 里；发送从主 isolate 直接写同一个 socket，
// 避免每帧都走一次端口往返。

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'watch_transport.dart';

// AF_BTH / BTPROTO_RFCOMM 在部分 SDK 头文件里没有导出常量，Windows 固定为 32 / 3。
const int _afBth = 32;
const int _sockStream = 1;
const int _btProtoRfcomm = 3;
const int _solSocket = 0xFFFF;
const int _soSndTimeout = 0x1005;
const int _lmemFixed = 0x0000;
const int _lmemZeroInit = 0x0040;
const int _receiveSize = 4096;

// isolate 内的一整块内存布局：sockaddr(30) / fd_set(520) / timeval(8) / 接收缓冲。
const int _fdSetOffset = 32;
const int _fdSetSize = 520;
const int _timevalOffset = 560;
const int _bufferOffset = 576;
const int _memorySize = _bufferOffset + _receiveSize;
const int _selectTimeoutUs = 20000;

// ---- ws2_32.dll -----------------------------------------------------------

final DynamicLibrary _ws2 = DynamicLibrary.open('ws2_32.dll');

typedef _WsaStartupNative = Int32 Function(Uint16, Pointer<Uint8>);
typedef _WsaStartupDart = int Function(int, Pointer<Uint8>);
typedef _WsaCleanupNative = Int32 Function();
typedef _WsaCleanupDart = int Function();
typedef _WsaLastErrorNative = Int32 Function();
typedef _WsaLastErrorDart = int Function();
typedef _SocketNative = IntPtr Function(Int32, Int32, Int32);
typedef _SocketDart = int Function(int, int, int);
typedef _ConnectNative = Int32 Function(IntPtr, Pointer<Uint8>, Int32);
typedef _ConnectDart = int Function(int, Pointer<Uint8>, int);
typedef _SendNative = Int32 Function(IntPtr, Pointer<Uint8>, Int32, Int32);
typedef _SendDart = int Function(int, Pointer<Uint8>, int, int);
typedef _RecvNative = Int32 Function(IntPtr, Pointer<Uint8>, Int32, Int32);
typedef _RecvDart = int Function(int, Pointer<Uint8>, int, int);
typedef _CloseSocketNative = Int32 Function(IntPtr);
typedef _CloseSocketDart = int Function(int);
typedef _SetSockOptNative = Int32 Function(
  IntPtr,
  Int32,
  Int32,
  Pointer<Uint8>,
  Int32,
);
typedef _SetSockOptDart = int Function(int, int, int, Pointer<Uint8>, int);
typedef _SelectNative = Int32 Function(
  Int32,
  Pointer<Uint8>,
  Pointer<Uint8>,
  Pointer<Uint8>,
  Pointer<Uint8>,
);
typedef _SelectDart = int Function(
  int,
  Pointer<Uint8>,
  Pointer<Uint8>,
  Pointer<Uint8>,
  Pointer<Uint8>,
);

final _wsaStartup = _ws2.lookupFunction<_WsaStartupNative, _WsaStartupDart>(
  'WSAStartup',
);
final _wsaCleanup = _ws2.lookupFunction<_WsaCleanupNative, _WsaCleanupDart>(
  'WSACleanup',
);
final _wsaLastError = _ws2
    .lookupFunction<_WsaLastErrorNative, _WsaLastErrorDart>('WSAGetLastError');
final _socket = _ws2.lookupFunction<_SocketNative, _SocketDart>('socket');
final _connect = _ws2.lookupFunction<_ConnectNative, _ConnectDart>('connect');
final _send = _ws2.lookupFunction<_SendNative, _SendDart>('send');
final _recv = _ws2.lookupFunction<_RecvNative, _RecvDart>('recv');
final _closeSocket = _ws2.lookupFunction<_CloseSocketNative, _CloseSocketDart>(
  'closesocket',
);
final _setSockOpt = _ws2.lookupFunction<_SetSockOptNative, _SetSockOptDart>(
  'setsockopt',
);
final _select = _ws2.lookupFunction<_SelectNative, _SelectDart>('select');

// ---- kernel32.dll ---------------------------------------------------------

final DynamicLibrary _kernel32 = DynamicLibrary.open('kernel32.dll');

typedef _LocalAllocNative = Pointer<Uint8> Function(Uint32, IntPtr);
typedef _LocalAllocDart = Pointer<Uint8> Function(int, int);
typedef _LocalFreeNative = Pointer<Uint8> Function(Pointer<Uint8>);
typedef _LocalFreeDart = Pointer<Uint8> Function(Pointer<Uint8>);

final _localAlloc = _kernel32
    .lookupFunction<_LocalAllocNative, _LocalAllocDart>('LocalAlloc');
final _localFree = _kernel32.lookupFunction<_LocalFreeNative, _LocalFreeDart>(
  'LocalFree',
);

Pointer<Uint8>? _alloc(int size) {
  final pointer = _localAlloc(_lmemFixed | _lmemZeroInit, size);
  return pointer.address == 0 ? null : pointer;
}

/// 经典蓝牙 SPP 传输（仅 Windows）。
class WindowsRfcommTransport implements WatchTransport {
  WindowsRfcommTransport(this.address, {this.channel = 1});

  final String address;
  final int channel;

  final StreamController<Uint8List> _incoming =
      StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();

  Isolate? _isolate;
  ReceivePort? _receivePort;
  Completer<void>? _ready;
  int? _handle;
  Pointer<Uint8>? _sendBuffer;

  /// 最近一次链路断开的原因（调试/日志用，可为空）。
  String? closeReason;

  @override
  String get displayName => '经典 SPP';

  @override
  bool get isConnected => _handle != null;

  @override
  Stream<Uint8List> get incoming => _incoming.stream;

  @override
  Stream<void> get closed => _closed.stream;

  @override
  Future<void> connect() async {
    if (_handle != null) return;

    final port = ReceivePort();
    _receivePort = port;
    final ready = Completer<void>();
    _ready = ready;
    port.listen(_onIsolateMessage);

    try {
      _isolate = await Isolate.spawn<List<Object?>>(_rfcommEntry, <Object?>[
        port.sendPort,
        address,
        channel,
      ]);
      await ready.future.timeout(const Duration(seconds: 30));
    } catch (error) {
      await close();
      if (error is TransportException) rethrow;
      throw TransportException('SPP 连接 $address 通道 $channel 失败：$error');
    }
  }

  void _onIsolateMessage(Object? message) {
    if (message is! List<Object?> || message.isEmpty) return;
    switch (message[0]) {
      case 'connected':
        _handle = message[1] as int;
        _sendBuffer ??= _alloc(512);
        final ready = _ready;
        if (ready != null && !ready.isCompleted) ready.complete();
      case 'data':
        final data = message[1];
        if (data is Uint8List && !_incoming.isClosed) _incoming.add(data);
      case 'error':
        final text = '${message[1]}';
        closeReason = text;
        final ready = _ready;
        if (ready != null && !ready.isCompleted) {
          ready.completeError(TransportException(text));
        } else {
          _handle = null;
          if (!_closed.isClosed) _closed.add(null);
        }
      case 'closed':
        closeReason = message.length > 1 ? '${message[1]}' : '链路已断开';
        _handle = null;
        if (!_closed.isClosed) _closed.add(null);
    }
  }

  @override
  Future<void> send(Uint8List bytes) async {
    final handle = _handle;
    final buffer = _sendBuffer;
    if (handle == null || buffer == null) {
      throw const TransportException('SPP 未连接');
    }
    var offset = 0;
    while (offset < bytes.length) {
      final remaining = bytes.length - offset;
      final take = remaining > 512 ? 512 : remaining;
      buffer.asTypedList(take).setAll(0, bytes.sublist(offset, offset + take));
      final written = _send(handle, buffer, take, 0);
      if (written <= 0) {
        throw TransportException('SPP 发送失败（错误码 ${_wsaLastError()}）');
      }
      offset += written;
    }
  }

  @override
  Future<void> close() async {
    final handle = _handle;
    _handle = null;
    if (handle != null) _closeSocket(handle);

    final buffer = _sendBuffer;
    _sendBuffer = null;
    if (buffer != null) _localFree(buffer);

    final ready = _ready;
    _ready = null;
    if (ready != null && !ready.isCompleted) {
      ready.completeError(const TransportException('SPP 连接已取消'));
    }

    final isolate = _isolate;
    _isolate = null;
    isolate?.kill(priority: Isolate.immediate);

    final port = _receivePort;
    _receivePort = null;
    port?.close();
  }

  @override
  Future<void> release() => close();
}

// ---- isolate 入口 ---------------------------------------------------------

void _rfcommEntry(List<Object?> args) {
  final mainPort = args[0] as SendPort;
  final address = args[1] as String;
  final channel = args[2] as int;

  Pointer<Uint8>? memory;
  var handle = -1;
  var announced = false;
  try {
    final startupData = _alloc(512);
    if (startupData == null) throw const TransportException('WSAStartup 分配失败');
    try {
      final rc = _wsaStartup(0x0202, startupData);
      if (rc != 0) throw TransportException('WSAStartup 失败（$rc）');
    } finally {
      _localFree(startupData);
    }

    final target = _parseAddress(address);
    final sockaddr = Uint8List(30);
    _writeUint16(sockaddr, 0, _afBth);
    _writeUint64(sockaddr, 2, target);
    _writeUint32(sockaddr, 26, channel);

    memory = _alloc(_memorySize);
    if (memory == null) throw const TransportException('RFCOMM 缓冲区分配失败');

    Object? lastError;
    for (var attempt = 0; attempt < 3; attempt++) {
      final socket = _socket(_afBth, _sockStream, _btProtoRfcomm);
      if (socket == -1) {
        lastError = 'socket 失败（错误码 ${_wsaLastError()}）';
        break;
      }
      memory.asTypedList(30).setAll(0, sockaddr);
      final rc = _connect(socket, memory, 30);
      if (rc == 0) {
        handle = socket;
        break;
      }
      lastError = 'connect 失败（错误码 ${_wsaLastError()}）';
      _closeSocket(socket);
      sleep(const Duration(milliseconds: 400));
    }
    if (handle == -1) {
      throw TransportException('SPP 连接 $address 通道 $channel 失败：$lastError');
    }

    _setTimeout(handle, _soSndTimeout, 3000, memory + 32);

    mainPort.send(<Object?>['connected', handle]);
    announced = true;

    final fdSet = memory + _fdSetOffset;
    final timeval = memory + _timevalOffset;
    final bufferPointer = memory + _bufferOffset;
    final buffer = bufferPointer.asTypedList(_receiveSize);
    while (true) {
      _prepareFdSet(fdSet, handle);
      _prepareTimeval(timeval, _selectTimeoutUs);
      final ready = _select(0, fdSet, nullptr, nullptr, timeval);
      if (ready == 0) continue;
      if (ready < 0) {
        mainPort.send(<Object?>['closed', 'select 失败（错误码 ${_wsaLastError()}）']);
        return;
      }

      final read = _recv(handle, bufferPointer, _receiveSize, 0);
      if (read > 0) {
        mainPort.send(<Object?>[
          'data',
          Uint8List.fromList(buffer.sublist(0, read)),
        ]);
        continue;
      }
      mainPort.send(<Object?>['closed', read == 0 ? '对端关闭连接' : 'recv 失败']);
      return;
    }
  } catch (error) {
    final text = error is TransportException ? error.message : '$error';
    mainPort.send(<Object?>[announced ? 'closed' : 'error', text]);
  } finally {
    if (handle != -1) _closeSocket(handle);
    if (memory != null) _localFree(memory);
    _wsaCleanup();
  }
}

void _setTimeout(
  int socket,
  int option,
  int milliseconds,
  Pointer<Uint8> buffer,
) {
  final bytes = buffer.asTypedList(4)
    ..[0] = milliseconds & 0xFF
    ..[1] = (milliseconds >> 8) & 0xFF
    ..[2] = (milliseconds >> 16) & 0xFF
    ..[3] = (milliseconds >> 24) & 0xFF;
  _setSockOpt(socket, _solSocket, option, buffer, bytes.length);
}

/// fd_set：u_int fd_count + SOCKET fd_array[64]（x64 下数组从偏移 8 开始）。
void _prepareFdSet(Pointer<Uint8> fdSet, int socket) {
  final bytes = fdSet.asTypedList(_fdSetSize);
  bytes.fillRange(0, _fdSetSize, 0);
  _writeUint32(bytes, 0, 1);
  _writeUint64(bytes, 8, socket);
}

/// timeval：两个 LONG（秒、微秒），select 会就地修改，每轮都要重置。
void _prepareTimeval(Pointer<Uint8> timeval, int microseconds) {
  final bytes = timeval.asTypedList(8);
  _writeUint32(bytes, 0, 0);
  _writeUint32(bytes, 4, microseconds);
}

int _parseAddress(String text) {
  final cleaned = text.replaceAll(RegExp('[:\\-\\s]'), '');
  if (cleaned.length != 12) {
    throw TransportException('蓝牙地址格式不对：$text');
  }
  return int.parse(cleaned, radix: 16);
}

void _writeUint16(Uint8List target, int offset, int value) {
  target[offset] = value & 0xFF;
  target[offset + 1] = (value >> 8) & 0xFF;
}

void _writeUint32(Uint8List target, int offset, int value) {
  for (var i = 0; i < 4; i++) {
    target[offset + i] = (value >> (8 * i)) & 0xFF;
  }
}

void _writeUint64(Uint8List target, int offset, int value) {
  for (var i = 0; i < 8; i++) {
    target[offset + i] = (value >> (8 * i)) & 0xFF;
  }
}
