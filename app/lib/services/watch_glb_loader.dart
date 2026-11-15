// AT32 手表助手 · GPL-3.0-or-later
//
// 纯 Dart glTF 2.0 (.glb) 3D 装配体加载器：
// 读取 `assets/models/at32_watch.glb`，提取焊接去重后的顶点与材质批次，
// 统一转换到手表本体系 Body B (+X 右/3点, +Y 上/12点, +Z 屏幕外法线)。

import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/services.dart';

/// 单个按材质与语义分组的三角面网格索引段。
class WatchMeshPart {
  const WatchMeshPart({
    required this.name,
    required this.indices,
    required this.colorRgb,
    required this.metallic,
    required this.doubleSided,
    required this.isGlass,
    required this.isLabel,
    required this.layerBias,
  });

  final String name;
  final Uint16List indices;
  final int colorRgb; // 0x00RRGGBB
  final bool metallic;
  final bool doubleSided;
  final bool isGlass;
  final bool isLabel;
  final double layerBias;
}

/// 解析完成、可直接被 GPU `Canvas.drawVertices` 实时渲染的手表 3D 模型。
class WatchGlbModel {
  const WatchGlbModel({
    required this.verticesBody,
    required this.parts,
    required this.radius,
    required this.glassBoundsBody,
  });

  /// 连续打包的手表本体系顶点 `[x0, y0, z0, x1, y1, z1, ...]`（单位：米）。
  final Float32List verticesBody;
  final List<WatchMeshPart> parts;

  /// 模型包围球半径（米），用于自适配缩放。
  final double radius;

  /// `TFT_GLASS` 屏幕显示区在手表本体系中的包围盒 `[minX, maxX, minY, maxY, topZ]`。
  final List<double> glassBoundsBody;

  int get vertexCount => verticesBody.length ~/ 3;

  static Future<WatchGlbModel>? _cached;

  /// 全局单例缓存加载 `assets/models/at32_watch.glb`。
  static Future<WatchGlbModel> loadDefault() {
    return _cached ??= _loadFromBundle();
  }

  static Future<WatchGlbModel> _loadFromBundle() async {
    final data = await rootBundle.load('assets/models/at32_watch.glb');
    return parseGlb(data);
  }

  /// 从内存 `.glb` 字节解析 WatchGlbModel（方便单元测试验证）。
  static WatchGlbModel parseGlb(ByteData data) {
    if (data.lengthInBytes < 28) {
      throw const FormatException('GLB 文件头不完整');
    }
    final magic = data.getUint32(0, Endian.little);
    final version = data.getUint32(4, Endian.little);
    if (magic != 0x46546C67 || version != 2) {
      throw const FormatException('不支持的 glTF 二进制版本');
    }

    var offset = 12;
    Map<String, dynamic>? gltf;
    Uint8List? binChunk;

    while (offset + 8 <= data.lengthInBytes) {
      final chunkLen = data.getUint32(offset, Endian.little);
      final chunkType = data.getUint32(offset + 4, Endian.little);
      final start = offset + 8;
      if (start + chunkLen > data.lengthInBytes) break;
      if (chunkType == 0x4E4F534A) {
        final jsonStr = utf8.decode(
          data.buffer.asUint8List(data.offsetInBytes + start, chunkLen),
        );
        gltf = jsonDecode(jsonStr) as Map<String, dynamic>;
      } else if (chunkType == 0x004E4942) {
        binChunk = data.buffer.asUint8List(
          data.offsetInBytes + start,
          chunkLen,
        );
      }
      offset = start + chunkLen;
    }

    if (gltf == null || binChunk == null) {
      throw const FormatException('GLB 缺少 JSON 或 BIN 数据块');
    }

    final accessors =
        (gltf['accessors'] as List<dynamic>? ?? const <dynamic>[])
            .cast<Map<String, dynamic>>();
    final bufferViews =
        (gltf['bufferViews'] as List<dynamic>? ?? const <dynamic>[])
            .cast<Map<String, dynamic>>();
    final materials =
        (gltf['materials'] as List<dynamic>? ?? const <dynamic>[])
            .cast<Map<String, dynamic>>();
    final meshes = (gltf['meshes'] as List<dynamic>? ?? const <dynamic>[])
        .cast<Map<String, dynamic>>();
    final nodes = (gltf['nodes'] as List<dynamic>? ?? const <dynamic>[])
        .cast<Map<String, dynamic>>();

    final binData = ByteData.sublistView(binChunk);

    Float32List readVec3(int accessorIdx) {
      final acc = accessors[accessorIdx];
      final bv = bufferViews[acc['bufferView'] as int];
      final base =
          (bv['byteOffset'] as int? ?? 0) + (acc['byteOffset'] as int? ?? 0);
      final stride = bv['byteStride'] as int? ?? 12;
      final count = acc['count'] as int;
      final out = Float32List(count * 3);
      for (var i = 0; i < count; i++) {
        final p = base + i * stride;
        // glTF (x, y, z) -> Body (x, -z, y)，行列式 +1，保持绕序。
        out[i * 3] = binData.getFloat32(p, Endian.little);
        out[i * 3 + 1] = -binData.getFloat32(p + 8, Endian.little);
        out[i * 3 + 2] = binData.getFloat32(p + 4, Endian.little);
      }
      return out;
    }

    List<int> readIndices(int accessorIdx) {
      final acc = accessors[accessorIdx];
      final bv = bufferViews[acc['bufferView'] as int];
      final base =
          (bv['byteOffset'] as int? ?? 0) + (acc['byteOffset'] as int? ?? 0);
      final count = acc['count'] as int;
      final comp = acc['componentType'] as int;
      final out = List<int>.filled(count, 0);
      if (comp == 5123) {
        for (var i = 0; i < count; i++) {
          out[i] = binData.getUint16(base + i * 2, Endian.little);
        }
      } else if (comp == 5125) {
        for (var i = 0; i < count; i++) {
          out[i] = binData.getUint32(base + i * 4, Endian.little);
        }
      } else if (comp == 5121) {
        for (var i = 0; i < count; i++) {
          out[i] = binData.getUint8(base + i);
        }
      }
      return out;
    }

    (int rgb, bool metallic) readMaterial(int matIdx) {
      if (matIdx < 0 || matIdx >= materials.length) {
        return (0xA8B0B5, false);
      }
      final mat = materials[matIdx];
      final pbr = mat['pbrMetallicRoughness'] as Map<String, dynamic>?;
      final factor = (pbr?['baseColorFactor'] as List<dynamic>?);
      var r = 165, g = 172, b = 178;
      if (factor != null && factor.length >= 3) {
        // 线性 sRGB 近似 gamma 提亮，使深绿 PCB 与深蓝模块在 OLED 暗底中通透有层次。
        double toSrgb(num v) => math.pow(v.clamp(0.0, 1.0), 1 / 2.0).toDouble();
        r = (toSrgb(factor[0] as num) * 255).round().clamp(0, 255);
        g = (toSrgb(factor[1] as num) * 255).round().clamp(0, 255);
        b = (toSrgb(factor[2] as num) * 255).round().clamp(0, 255);
      }
      final metalFactor = (pbr?['metallicFactor'] as num?)?.toDouble() ?? 0.0;
      final matName = '${mat['name'] ?? ''}';
      final isMetal =
          metalFactor > 0.3 ||
          matName == 'mod_metal' ||
          matName == 'mtl1' ||
          matName == 'mtl17' ||
          matName == 'mtl11' ||
          matName == 'mtl10' ||
          matName == 'mtl4';
      return ((r << 16) | (g << 8) | b, isMetal);
    }

    final mergedVerts = <double>[];
    final parts = <WatchMeshPart>[];
    var maxR2 = 0.0;
    var gx0 = 1e9, gx1 = -1e9, gy0 = 1e9, gy1 = -1e9, gz1 = -1e9;

    for (final node in nodes) {
      final meshIdx = node['mesh'] as int?;
      if (meshIdx == null || meshIdx < 0 || meshIdx >= meshes.length) continue;
      final nodeName = '${node['name'] ?? 'part'}';
      final mesh = meshes[meshIdx];
      final primitives = (mesh['primitives'] as List<dynamic>? ?? const [])
          .cast<Map<String, dynamic>>();

      final isGlass = nodeName == 'TFT_GLASS';
      final isLabel = nodeName.startsWith('LBL_');
      final isPcb = nodeName == 'PCB';
      final bias = isGlass
          ? 0.0008
          : (isLabel
                ? 0.0004
                : (nodeName == 'TFT_BEZEL' ? 0.00025 : 0.0));

      for (final prim in primitives) {
        final attrs = prim['attributes'] as Map<String, dynamic>;
        final posIdx = attrs['POSITION'] as int?;
        final idxAcc = prim['indices'] as int?;
        if (posIdx == null || idxAcc == null) continue;

        final pos = readVec3(posIdx);
        final rawIdx = readIndices(idxAcc);
        final baseVertex = mergedVerts.length ~/ 3;

        for (var i = 0; i < pos.length; i += 3) {
          final x = pos[i];
          final y = pos[i + 1];
          final z = pos[i + 2];
          mergedVerts.add(x);
          mergedVerts.add(y);
          mergedVerts.add(z);
          final r2 = x * x + y * y + z * z;
          if (r2 > maxR2) maxR2 = r2;
          if (isGlass) {
            if (x < gx0) gx0 = x;
            if (x > gx1) gx1 = x;
            if (y < gy0) gy0 = y;
            if (y > gy1) gy1 = y;
            if (z > gz1) gz1 = z;
          }
        }

        final (rgb, metallic) = readMaterial(prim['material'] as int? ?? -1);
        final shiftedIdx = Uint16List(rawIdx.length);
        for (var i = 0; i < rawIdx.length; i++) {
          shiftedIdx[i] = baseVertex + rawIdx[i];
        }

        parts.add(
          WatchMeshPart(
            name: nodeName,
            indices: shiftedIdx,
            colorRgb: rgb,
            metallic: metallic,
            doubleSided: isPcb,
            isGlass: isGlass,
            isLabel: isLabel,
            layerBias: bias,
          ),
        );
      }
    }

    if (gx0 > gx1) {
      gx0 = -0.014;
      gx1 = 0.016;
      gy0 = -0.0117;
      gy1 = 0.0317;
      gz1 = 0.0105;
    }

    return WatchGlbModel(
      verticesBody: Float32List.fromList(mergedVerts),
      parts: parts,
      radius: math.sqrt(maxR2),
      glassBoundsBody: <double>[gx0, gx1, gy0, gy1, gz1],
    );
  }
}
