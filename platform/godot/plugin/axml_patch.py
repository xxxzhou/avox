#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""二进制 AndroidManifest.xml (AXML) 注入 magnet deep-link intent-filter。

策略 (append-only, 原节点字节零改动):
  - 新字符串追加到 string pool 尾部 (不改动既有索引, 所有原节点引用保持有效)
  - resmap 以 0 填充扩展到新属性名索引, 并补 android:scheme 的资源 id
  - 在 launcher activity-alias 的 </activity-alias> 前插入:
      <intent-filter>
        <action android:name="android.intent.action.VIEW"/>
        <category android:name="android.intent.category.DEFAULT"/>
        <category android:name="android.intent.category.BROWSABLE"/>
        <data android:scheme="magnet"/>
      </intent-filter>
幂等: pool 里已有 "magnet" 时原样返回。

节点布局 (ResXMLTree_node):
  [type u16][headerSize u16=16][size u32][line u32][comment u32]
  START_ELEMENT ext: [ns u32][name u32][attributeStart u16=0x14][attributeSize u16=0x14]
                     [attributeCount u16][idIndex u16][classIndex u16][styleIndex u16]  (20B)
  attribute (20B):   [ns u32][name u32][rawValue u32][typedValue: size u16][res0 u8][type u8][data u32]
  END_ELEMENT ext:   [ns u32][name u32]  (8B)
"""
import struct

RES_XML_TYPE = 0x0003
RES_STRING_POOL_TYPE = 0x0001
RES_XML_RESOURCE_MAP_TYPE = 0x0180
RES_START_ELEMENT = 0x0102
RES_END_ELEMENT = 0x0103
UTF8_FLAG = 0x100

ATTR_SCHEME_RES_ID = 0x01010027  # android:scheme (0x01010026 是 mimeType, 写错会被解析成 mimeType 导致 INSTALL_PARSE_FAILED_MANIFEST_MALFORMED)
NO_INDEX = 0xFFFFFFFF


def _parse_string_pool(data: bytes, off: int):
    typ, hs, size = struct.unpack_from("<HHI", data, off)
    cnt, styles, flags, str_start, _sty = struct.unpack_from("<IIIII", data, off + 8)
    utf8 = bool(flags & UTF8_FLAG)
    offs = struct.unpack_from("<%dI" % cnt, data, off + hs)
    strings = []
    base = off + str_start
    for o in offs:
        p = base + o
        if utf8:
            n = data[p]
            s = data[p + 2:p + 2 + n].decode("utf-8", "replace")
        else:
            n = struct.unpack_from("<H", data, p)[0]
            s = data[p + 2:p + 2 + n * 2].decode("utf-16-le", "replace")
        strings.append(s)
    return strings, utf8, size


def _build_string_pool(strings, utf8=False):
    flags = UTF8_FLAG if utf8 else 0
    header_size = 28
    offs = []
    body = bytearray()
    for s in strings:
        offs.append(len(body))
        if utf8:
            enc = s.encode("utf-8")
            body += bytes([len(enc), 0]) + enc + bytes([0])
        else:
            enc = s.encode("utf-16-le")
            body += struct.pack("<H", len(s)) + enc + struct.pack("<H", 0)
        while len(body) % 4:
            body += b"\x00"
    strings_start = header_size + 4 * len(offs)
    size = strings_start + len(body)
    head = struct.pack(
        "<HHIIIIII", RES_STRING_POOL_TYPE, header_size, size,
        len(strings), 0, flags, strings_start, 0)
    return head + struct.pack("<%dI" % len(offs), *offs) + bytes(body)


def _parse_attrs(data: bytes, pos: int, attr_count: int):
    attrs = []
    for i in range(attr_count):
        a = pos + i * 20
        ns, name, raw = struct.unpack_from("<III", data, a)
        vsize, res0, dtype, vdata = struct.unpack_from("<HBBI", data, a + 12)
        attrs.append({"ns": ns, "name": name, "raw": raw,
                      "dtype": dtype, "data": vdata})
    return attrs


def add_magnet_filter(manifest: bytes) -> bytes:
    strings, utf8, pool_size = _parse_string_pool(manifest, 8)
    if "magnet" in strings:
        return manifest  # 已注入, 幂等

    off = 8 + pool_size
    map_typ, _hs, map_size = struct.unpack_from("<HHI", manifest, off)
    if map_typ != RES_XML_RESOURCE_MAP_TYPE:
        raise RuntimeError("resmap 位置异常: %04x" % map_typ)
    map_ids = list(struct.unpack_from("<%dI" % ((map_size - 8) // 4),
                                      manifest, off + 8))
    body_off = off + map_size

    def pool_idx(s: str) -> int:
        if s in strings:
            return strings.index(s)
        strings.append(s)
        return len(strings) - 1

    # ── 定位 launcher activity-alias 的 END 节点 ──
    name_idx = pool_idx("name")  # 已存在, 不会追加
    alias_value_idx = pool_idx("com.godot.game.GodotAppLauncher")  # 已存在
    ns_android = NO_INDEX
    pos = body_off
    insert_at = None
    alias_depth = None
    depth = 0
    while pos + 8 <= len(manifest):
        typ, hs, size = struct.unpack_from("<HHI", manifest, pos)
        if size <= 0:
            break
        if typ == RES_START_ELEMENT:
            ns, ename = struct.unpack_from("<II", manifest, pos + 16)
            attr_start, _as, attr_count = struct.unpack_from("<HHH", manifest, pos + 24)
            # attributeStart 相对 ext 结构 (节点头 16B 之后)
            attrs = _parse_attrs(manifest, pos + 16 + attr_start, attr_count)
            if ns_android == NO_INDEX and attrs:
                ns_android = attrs[0]["ns"]
            if alias_depth is None:
                for a in attrs:
                    if a["name"] == name_idx and a["dtype"] == 3 \
                            and a["data"] == alias_value_idx:
                        alias_depth = depth  # 记录 alias 的层级
                        break
            depth += 1
        elif typ == RES_END_ELEMENT:
            if alias_depth is not None and depth == alias_depth + 1:
                insert_at = pos  # alias 的 </activity-alias>
                break
            depth -= 1
        pos += size
    if insert_at is None:
        raise RuntimeError("未找到 launcher activity-alias")

    # ── 字符串与 resmap ──
    s_view = pool_idx("android.intent.action.VIEW")
    s_defcat = pool_idx("android.intent.category.DEFAULT")
    s_brow = pool_idx("android.intent.category.BROWSABLE")
    s_magnet = pool_idx("magnet")
    s_if = pool_idx("intent-filter")
    s_action = pool_idx("action")
    s_category = pool_idx("category")
    s_data = pool_idx("data")
    s_name = name_idx
    s_scheme = pool_idx("scheme")  # 新属性名
    while len(map_ids) <= s_scheme:
        map_ids.append(0)
    map_ids[s_scheme] = ATTR_SCHEME_RES_ID

    # ── 节点序列化 ──
    def sattr(name_i: int, value_i: int):
        return {"ns": ns_android, "name": name_i, "raw": value_i,
                "dtype": 3, "data": value_i}

    def start_el(name_i: int, attrs, line: int) -> bytes:
        # 元素名不带命名空间 (ns=NO_INDEX); 属性才挂 android ns
        ext = struct.pack("<IIHHHHHH", NO_INDEX, name_i, 0x14, 0x14,
                          len(attrs), 0, 0, 0)
        abody = b"".join(
            struct.pack("<IIIHBBI", a["ns"], a["name"], a["raw"],
                        8, 0, a["dtype"], a["data"]) for a in attrs)
        total = 16 + len(ext) + len(abody)
        return struct.pack("<HHI", RES_START_ELEMENT, 16, total) + \
            struct.pack("<II", line, NO_INDEX) + ext + abody

    def end_el(name_i: int, line: int) -> bytes:
        return struct.pack("<HHI", RES_END_ELEMENT, 16, 24) + \
            struct.pack("<II", line, NO_INDEX) + \
            struct.pack("<II", NO_INDEX, name_i)

    line = 900
    nodes = b""
    nodes += start_el(s_if, [], line); line += 1
    nodes += start_el(s_action, [sattr(s_name, s_view)], line)
    nodes += end_el(s_action, line); line += 1
    nodes += start_el(s_category, [sattr(s_name, s_defcat)], line)
    nodes += end_el(s_category, line); line += 1
    nodes += start_el(s_category, [sattr(s_name, s_brow)], line)
    nodes += end_el(s_category, line); line += 1
    nodes += start_el(s_data, [sattr(s_scheme, s_magnet)], line)
    nodes += end_el(s_data, line); line += 1
    nodes += end_el(s_if, line)

    # ── 重组 ──
    pool_chunk = _build_string_pool(strings, utf8)
    ids_bytes = struct.pack("<%dI" % len(map_ids), *map_ids)
    resmap_chunk = struct.pack("<HHI", RES_XML_RESOURCE_MAP_TYPE, 8,
                               8 + len(ids_bytes)) + ids_bytes
    body = manifest[body_off:insert_at] + nodes + manifest[insert_at:]
    out = struct.pack("<HHI", RES_XML_TYPE, 8,
                      8 + len(pool_chunk) + len(resmap_chunk) + len(body))
    return out + pool_chunk + resmap_chunk + body


def patch_apk_manifest(apk_path: str) -> bool:
    """注入并重写 APK 的 AndroidManifest.xml; 返回是否发生注入"""
    import shutil
    import zipfile
    changed = False
    tmp = apk_path + ".manifest_patch"
    with zipfile.ZipFile(apk_path) as zin, \
            zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == "AndroidManifest.xml":
                new = add_magnet_filter(data)
                if new != data:
                    changed = True
                    data = new
                    print("OK manifest 注入 magnet intent-filter")
            zout.writestr(item, data)
    shutil.move(tmp, apk_path)
    return changed


if __name__ == "__main__":
    import sys
    patch_apk_manifest(sys.argv[1])
