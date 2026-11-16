// AT32 手表助手 · GPL-3.0-or-later
//
// 跨平台图片选择桥接（Windows 使用原生 Win32 comdlg32.dll GetOpenFileNameW，
// Android 通过 MainActivity MethodChannel 打开系统文档/相册选择器，两端交互完全一致）。

import 'dart:ffi';
import 'dart:io';

import 'package:flutter/services.dart';

abstract final class ImagePickerBridge {
  static const MethodChannel _channel = MethodChannel(
    'watch_companion/bluetooth',
  );

  /// 弹出原生图片选择框并返回所选文件字节；取消或无系统文件框时返回 null。
  static Future<({String name, Uint8List bytes})?> pickImage() async {
    if (Platform.isAndroid) {
      final res = await _channel.invokeMethod<Map<Object?, Object?>>('pickImage');
      if (res == null) return null;
      final raw = res['bytes'];
      if (raw is! Uint8List || raw.isEmpty) return null;
      final name = '${res['name'] ?? 'photo'}';
      return (name: name, bytes: raw);
    }
    if (Platform.isWindows) {
      final path = _pickWindowsImagePath();
      if (path == null || path.isEmpty) return null;
      final file = File(path);
      if (!await file.exists()) return null;
      final bytes = await file.readAsBytes();
      final name = path.split(Platform.pathSeparator).last;
      return (name: name, bytes: bytes);
    }
    return null;
  }
}

// ---- Windows Win32 GetOpenFileNameW (comdlg32.dll) --------------------------

final class _OpenFileNameW extends Struct {
  @Uint32()
  external int lStructSize;
  @IntPtr()
  external int hwndOwner;
  @IntPtr()
  external int hInstance;
  external Pointer<Uint16> lpstrFilter;
  external Pointer<Uint16> lpstrCustomFilter;
  @Uint32()
  external int nMaxCustFilter;
  @Uint32()
  external int nFilterIndex;
  external Pointer<Uint16> lpstrFile;
  @Uint32()
  external int nMaxFile;
  external Pointer<Uint16> lpstrFileTitle;
  @Uint32()
  external int nMaxFileTitle;
  external Pointer<Uint16> lpstrInitialDir;
  external Pointer<Uint16> lpstrTitle;
  @Uint32()
  external int flags;
  @Uint16()
  external int nFileOffset;
  @Uint16()
  external int nFileExtension;
  external Pointer<Uint16> lpstrDefExt;
  @IntPtr()
  external int lCustData;
  @IntPtr()
  external int lpfnHook;
  external Pointer<Uint16> lpTemplateName;
  @IntPtr()
  external int pvReserved;
  @Uint32()
  external int dwReserved;
  @Uint32()
  external int flagsEx;
}

typedef _GetOpenFileNameWNative = Int32 Function(Pointer<_OpenFileNameW>);
typedef _GetOpenFileNameWDart = int Function(Pointer<_OpenFileNameW>);
typedef _LocalAllocNative = Pointer<Uint8> Function(Uint32, IntPtr);
typedef _LocalAllocDart = Pointer<Uint8> Function(int, int);
typedef _LocalFreeNative = Pointer<Uint8> Function(Pointer<Uint8>);
typedef _LocalFreeDart = Pointer<Uint8> Function(Pointer<Uint8>);

String? _pickWindowsImagePath() {
  try {
    final kernel32 = DynamicLibrary.open('kernel32.dll');
    final comdlg32 = DynamicLibrary.open('comdlg32.dll');
    final localAlloc = kernel32.lookupFunction<_LocalAllocNative, _LocalAllocDart>(
      'LocalAlloc',
    );
    final localFree = kernel32.lookupFunction<_LocalFreeNative, _LocalFreeDart>(
      'LocalFree',
    );
    final getOpenFileNameW = comdlg32
        .lookupFunction<_GetOpenFileNameWNative, _GetOpenFileNameWDart>(
          'GetOpenFileNameW',
        );

    const maxPathChars = 512;
    const filter = '图片文件 (*.png;*.jpg;*.jpeg;*.bmp)\x00*.png;*.jpg;*.jpeg;*.bmp\x00所有文件 (*.*)\x00*.*\x00\x00';
    const title = '选择自定义表盘图片 (将自动裁切压缩为 128×160)\x00';

    final ofnMem = localAlloc(0x0040, sizeOf<_OpenFileNameW>());
    final fileBuf = localAlloc(0x0040, maxPathChars * 2).cast<Uint16>();
    final filterPtr = _allocUtf16(localAlloc, filter);
    final titlePtr = _allocUtf16(localAlloc, title);
    try {
      final ofn = ofnMem.cast<_OpenFileNameW>();
      ofn.ref.lStructSize = sizeOf<_OpenFileNameW>();
      ofn.ref.lpstrFilter = filterPtr;
      ofn.ref.nFilterIndex = 1;
      ofn.ref.lpstrFile = fileBuf;
      ofn.ref.nMaxFile = maxPathChars;
      ofn.ref.lpstrTitle = titlePtr;
      // OFN_FILEMUSTEXIST(0x1000) | OFN_PATHMUSTEXIST(0x0800) | OFN_NOCHANGEDIR(0x0008)
      ofn.ref.flags = 0x00001808;

      final ok = getOpenFileNameW(ofn);
      if (ok == 0) return null;
      final units = <int>[];
      for (var i = 0; i < maxPathChars; i++) {
        final u = fileBuf[i];
        if (u == 0) break;
        units.add(u);
      }
      return String.fromCharCodes(units);
    } finally {
      localFree(ofnMem);
      localFree(fileBuf.cast());
      localFree(filterPtr.cast());
      localFree(titlePtr.cast());
    }
  } catch (_) {
    return null;
  }
}

Pointer<Uint16> _allocUtf16(_LocalAllocDart alloc, String text) {
  final units = text.codeUnits;
  final ptr = alloc(0x0040, units.length * 2 + 2).cast<Uint16>();
  for (var i = 0; i < units.length; i++) {
    ptr[i] = units[i];
  }
  return ptr;
}
