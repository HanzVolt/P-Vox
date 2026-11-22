#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-only accounting of one built FM-1 package.

Reads the .fwsc the linker just produced, decrypts it with the same cipher the
packer used, and compares the JLFS app.bin entry with volt.bin. Spare is reported
only when those bytes match and the entry size, the linker map, and APP_SLOT agree.
A mismatch prints unknown and exits 2. This does not flash and does not time the chip.

  python3 tools/measure_flash.py build/volt.fwsc build/volt.bin
"""
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))
import fm1_install  # noqa: E402
import fm1pkg_make  # noqa: E402

APP_XIP = 0x02000120
BANNED = (
    "libfm1_engine",
    "fm_core_render",
    "dx7note_compute",
    "libfm1",
    "fm1_engine.a",
)
STUBS = (
    "inst_polivoks",
    "inst_altair231",
    "inst_aelita",
    "inst_rmif_chorus",
    "inst_rmif_phaser",
)


def unpack_entry(raw):
    crc, dcrc, offset, size, flags, resvd, index, name = struct.unpack("<HHIIBBH16s", raw[:32])
    body_crc = fm1pkg_make.crc16(raw[2:32])
    text = name.split(b"\0", 1)[0]
    try:
        label = text.decode("ascii")
    except UnicodeDecodeError:
        label = text.hex()
    return {
        "crc_ok": body_crc == crc,
        "dcrc": dcrc,
        "offset": offset,
        "size": size,
        "flags": flags,
        "resvd": resvd,
        "index": index,
        "name": label,
        "name_raw": name,
    }


def package_files(image):
    hdr = fm1_install._dec(image[0:0x40])
    if fm1pkg_make.crc16(hdr[2:0x40]) != int.from_bytes(hdr[0:2], "little"):
        raise SystemExit("measure: ufw header CRC failed")
    count = int.from_bytes(hdr[8:10], "little")
    out = []
    lst = image[0x40:0x40 + 0x50 * count]
    for i in range(count):
        e = fm1_install._dec(lst[i * 0x50:(i + 1) * 0x50])
        typ = int.from_bytes(e[0:2], "little")
        off = int.from_bytes(e[8:12], "little")
        size = int.from_bytes(e[12:16], "little")
        name = e[0x40:0x50].split(b"\0", 1)[0].decode("ascii", "replace")
        out.append((typ, name, bytes(image[off:off + size])))
    return out


def sfc_inplace(buf, key):
    """Inverse of fm1pkg_make.sfc: the cipher is XOR."""
    fm1pkg_make.sfc(buf, 0, len(buf), 0, key)


def load_map_length(map_text):
    m = re.search(r"^XIP\s+0x0*2000120\s+0x([0-9a-f]+)", map_text, re.M | re.I)
    return int(m.group(1), 16) if m else None


def section_rows(objdump_text):
    """JieLi objdump -h: Idx Name Size Address Type... (no separate LMA column)."""
    rows = []
    for ln in objdump_text.splitlines():
        p = ln.split()
        if len(p) >= 4 and p[1].startswith(".") and re.fullmatch(r"[0-9a-fA-F]+", p[2]) and re.fullmatch(r"[0-9a-fA-F]+", p[3]):
            rows.append((p[1], int(p[2], 16), int(p[3], 16)))
    return rows


def sym_addr(sym_text, name):
    m = re.search(r"^([0-9a-f]+).*\s" + name + r"$", sym_text, re.M)
    return int(m.group(1), 16) if m else None


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: measure_flash.py PACKAGE.fwsc APP.bin")
    pkg_path, app_path = Path(sys.argv[1]), Path(sys.argv[2])
    raw = pkg_path.read_bytes()
    app = app_path.read_bytes()
    product = fm1_install.product_of(raw)
    image, blocks = fm1_install.deinterleave(raw)
    damage = fm1_install.package_damage(image)
    if damage:
        raise SystemExit(f"measure: package damaged ({damage})")
    files = package_files(image)
    flash = next(d for typ, name, d in files if typ == 0 and name == "flash.bin")
    ota = next(d for typ, name, d in files if typ == 100)
    fm1pkg_make.set_board("fm1")
    key = fm1pkg_make.KEY
    if len(flash) != fm1pkg_make.FLASH_SIZE:
        print(f"NOTE flash.bin length {len(flash)} != FLASH_SIZE {fm1pkg_make.FLASH_SIZE:#x}")

    # First 32-byte SFC block is the app_area_head entry. Decrypt only that, read blk, then
    # decide the encrypted region length from the nested cfg that follows blk.
    head = bytearray(flash[0x4000:0x4020])
    sfc_inplace(head, key)
    area_head = unpack_entry(head)
    if not area_head["crc_ok"] or area_head["name"] != "app_area_head":
        raise SystemExit(
            f"measure: app_area_head did not decrypt (name={area_head['name']!r} crc_ok={area_head['crc_ok']}). "
            "Spare unknown.")
    blk = area_head["size"]
    # Nested cfg sits immediately after blk and is part of the same SFC region. Its length is
    # not stored in app_area_head. Find it by requiring the bytes after a candidate end to be
    # the unencrypted 0xFF tail the packer left up to FLASH_SIZE. Candidate: decrypt a window
    # past blk and parse the cfg entry, whose size field is the cfg directory length.
    window = bytearray(flash[0x4000:min(len(flash), 0x4000 + blk + 4096)])
    sfc_inplace(window, key)
    if blk + 32 > len(window):
        raise SystemExit("measure: flash image shorter than app_area_head size. Spare unknown.")
    cfg_ent = unpack_entry(window[blk:blk + 32])
    if not cfg_ent["crc_ok"] or cfg_ent["name"] != "cfg":
        raise SystemExit(
            f"measure: cfg directory did not parse (name={cfg_ent['name']!r}). Spare unknown.")
    cfg_len = cfg_ent["size"]  # entry() stores 0x20 + len(cfg_body); the field is the body size
    # fm1pkg_make.entry for cfg: size argument is 0x20 + len(cfg_body), and the entry itself
    # is another 32 bytes in front of that body. Total appended bytes = 32 + size? 
    # Look at the source: cfg = entry(...) + cfg_body, and the size field is 0x20 + len(cfg_body).
    # entry() is 32 bytes. cfg_body length is size - 0x20. Total = 32 + (size - 0x20) = size + 12?
    # struct size field is 0x20+len(cfg_body) which EQUALS len(entry)+len(cfg_body) - wait.
    # entry is 32 = 0x20 bytes. size field = 0x20 + len(cfg_body) = len(cfg). 
    # So the whole cfg blob written after area is `cfg` whose length is the size field.
    region_len = blk + cfg_len
    if 0x4000 + region_len > len(flash):
        raise SystemExit("measure: cfg size runs off flash.bin. Spare unknown.")
    tail = flash[0x4000 + region_len:]
    if any(b != 0xFF for b in tail):
        raise SystemExit(
            "measure: bytes after the parsed app region are not 0xFF. "
            "The region length is not verified. Spare unknown.")
    region = bytearray(flash[0x4000:0x4000 + region_len])
    sfc_inplace(region, key)
    # Re-parse from the fully decrypted region (the window above was a prefix and matches).
    area_head = unpack_entry(region[0:32])
    if fm1pkg_make.crc16(region[32:blk]) != area_head["dcrc"]:
        raise SystemExit("measure: app_area_head payload CRC failed. Spare unknown.")

    ents = []
    o = 32
    while o + 32 <= min(blk, 0x120):
        if region[o:o + 32] == b"\xff" * 32:
            break
        ent = unpack_entry(region[o:o + 32])
        if not ent["crc_ok"]:
            raise SystemExit(f"measure: directory entry at {o:#x} has a bad CRC. Spare unknown.")
        ents.append(ent)
        o += 32
    by_name = {e["name"]: e for e in ents}
    if "app.bin" not in by_name or "cfg_tool.bin" not in by_name:
        raise SystemExit(f"measure: directory names {[e['name'] for e in ents]}. Spare unknown.")
    app_ent = by_name["app.bin"]
    cfg_tool_ent = by_name["cfg_tool.bin"]
    slot = app_ent["size"]
    off = app_ent["offset"]
    if off != 0x120:
        raise SystemExit(f"measure: app.bin offset is {off:#x}, not 0x120. Spare unknown.")
    if off + slot > blk:
        raise SystemExit("measure: app.bin entry extends past the app_area block. Spare unknown.")
    packed = bytes(region[off:off + slot])
    cfg_tool = bytes(region[cfg_tool_ent["offset"]:cfg_tool_ent["offset"] + cfg_tool_ent["size"]])
    if fm1pkg_make.crc16(packed) != app_ent["dcrc"]:
        raise SystemExit("measure: packed app.bin CRC != directory. Spare unknown.")
    if fm1pkg_make.crc16(cfg_tool) != cfg_tool_ent["dcrc"]:
        raise SystemExit("measure: cfg_tool.bin CRC != directory. Spare unknown.")
    eq_info = None
    if cfg_ent["crc_ok"] and blk + cfg_len <= len(region):
        cfg_blob = bytes(region[blk:blk + cfg_len])
        if fm1pkg_make.crc16(cfg_blob[32:]) != cfg_ent["dcrc"]:
            raise SystemExit("measure: nested cfg CRC != directory. Spare unknown.")
        eq_ent = unpack_entry(cfg_blob[32:64])
        if not eq_ent["crc_ok"] or eq_ent["name"] != "eq_cfg_hw.bin":
            raise SystemExit("measure: eq_cfg_hw.bin entry missing. Spare unknown.")
        eq = cfg_blob[eq_ent["offset"]:eq_ent["offset"] + eq_ent["size"]]
        if fm1pkg_make.crc16(eq) != eq_ent["dcrc"]:
            raise SystemExit("measure: eq_cfg_hw.bin CRC != directory. Spare unknown.")
        eq_info = (eq_ent, hashlib.sha256(eq).hexdigest(), len(eq))

    map_path = app_path.with_suffix(".map")
    map_len = load_map_length(map_path.read_text(errors="replace")) if map_path.exists() else None
    ld_len = None
    ld_text = (SRC / "firmware" / "app.ld").read_text()
    m = re.search(r"XIP\s+\(rx\)\s*:\s*ORIGIN = 0x02000120,\s*LENGTH = (0x[0-9A-Fa-f]+)", ld_text)
    if m:
        ld_len = int(m.group(1), 16)
    code_slot = fm1pkg_make.APP_SLOT

    verified = (
        len(app) <= slot
        and packed[:len(app)] == app
        and packed[len(app):] == b"\xff" * (slot - len(app))
        and slot == code_slot
        and ld_len == code_slot
        and map_len == code_slot
    )
    ff_inside = app.count(0xFF)
    lines = []
    lines.append("FM-1 flash accounting (decrypted package, host-only)")
    lines.append(f"package {pkg_path}  {len(raw)} B  identity {product}  interleave blocks {blocks}")
    lines.append(f"flash.bin {len(flash)} B   ota.bin {len(ota)} B")
    lines.append(f"chip key used to decrypt the app area: {key:#06x} (fm1pkg_make.KEY)")
    lines.append("")
    lines.append("Capacity sources (they must all be the app.bin slot, not the file size):")
    lines.append(f"  fm1pkg_make.BOARDS['fm1'] APP_SLOT     {code_slot:#x} ({code_slot})")
    lines.append(f"  firmware/app.ld XIP LENGTH              {ld_len:#x}" if ld_len is not None else "  app.ld LENGTH  missing")
    lines.append(f"  build map XIP LENGTH                    {map_len:#x}" if map_len is not None else "  link map XIP LENGTH  missing")
    lines.append(f"  JLFS app.bin entry size in the package  {slot:#x} ({slot}) at area+{off:#x}")
    lines.append(f"  JLFS entry CRC matches packed slot      yes")
    lines.append("")
    lines.append("Where the slot sits. FLASH_SIZE is the end of what the update loader writes")
    lines.append("(fm1pkg_make.BOARDS fm1, PRCT). The head below 0x4000 is not the app slot.")
    lines.append(f"  flash head [0, 0x4000)                         {0x4000} B  (SPL, JLFS, not app code)")
    lines.append(f"  app area starts at flash                    0x4000")
    lines.append(f"  app_area_head block (directory + slot + cfg_tool)  {blk:#x} ({blk}) B")
    lines.append(f"  bytes before app payload inside that block  {off:#x} ({off}) B  directory and pad")
    lines.append(f"  cfg_tool.bin JLFS size                      {cfg_tool_ent['size']} B at area+{cfg_tool_ent['offset']:#x}")
    lines.append(f"  cfg_tool.bin sha256                         {hashlib.sha256(cfg_tool).hexdigest()}")
    lines.append(f"  nested cfg directory after the block        {cfg_len} B (entry size field)")
    if eq_info:
        eq_ent, eq_sha, eq_len = eq_info
        lines.append(f"  eq_cfg_hw.bin inside that directory         {eq_len} B  sha256 {eq_sha}")
        lines.append(f"  32 + 32 + eq length = {32 + 32 + eq_len} B; nested directory is {cfg_len} B")
    region_end = 0x4000 + region_len
    lines.append(f"  encrypted region ends at flash              {region_end:#x}")
    lines.append(f"  0xFF tail from there to FLASH_SIZE {fm1pkg_make.FLASH_SIZE:#x}   {len(tail)} B")
    lines.append("  That tail is outside the app.bin entry. Growing volt.bin does not consume it")
    lines.append("  unless APP_SLOT and app.ld change, which also moves cfg_tool.bin.")
    lines.append("")
    if verified:
        spare = slot - len(app)
        lines.append(f"USED app image (volt.bin, including in-image alignment gaps)  {len(app)} B")
        lines.append(f"  of which 0xFF bytes inside the image (gaps or pad, still USED)  {ff_inside}")
        lines.append(f"SLOT capacity                                               {slot} B")
        lines.append(f"SPARE inside the app.bin entry                              {spare} B")
        lines.append(f"spare >= 512 B                                              {'yes' if spare >= 512 else 'no'}")
        lines.append("The spare bytes in the decrypted entry are 0xFF and match the packer's tail pad.")
    else:
        lines.append("SPARE unknown: the image, the JLFS entry, the linker map, and APP_SLOT did not all agree.")
        lines.append(f"  len(volt.bin)={len(app)} slot={slot} APP_SLOT={code_slot} ld={ld_len} map={map_len}")
        lines.append(f"  prefix match={packed[:len(app)] == app} tail_is_ff={packed[len(app):] == b'\\xff' * (slot - len(app)) if len(app) <= slot else False}")
    lines.append("")
    lines.append("Directory entries inside the app area:")
    for e in ents:
        lines.append(f"  {e['name']:<16} off={e['offset']:#8x} size={e['size']:<8} flags={e['flags']:#04x} crc_ok={e['crc_ok']}")

    elf = app_path.with_suffix(".elf")
    tc = Path(os_toolchain())
    objdump = tc / "common" / "bin" / "objdump"
    if elf.exists() and objdump.exists():
        hdr = subprocess.check_output([str(objdump), "-h", str(elf)], text=True, errors="replace")
        lines.append("")
        lines.append("ELF section headers (objdump -h). This objdump prints VMA, not a separate LMA column.")
        for name, size, vma in section_rows(hdr):
            lines.append(f"  {name:<16} size={size:<8} vma={vma:#010x}")

    sym = app_path.with_suffix(".sym").read_text(errors="replace") if app_path.with_suffix(".sym").exists() else ""
    mp = map_path.read_text(errors="replace") if map_path.exists() else ""
    blob = sym + "\n" + mp
    lines.append("")
    lines.append("Stock-engine linkage:")
    lines.append("  link inputs are printed by tools/build.py; the map's object list follows.")
    linked = sorted(set(re.findall(r"[\w./+-]+\.o\b", mp)))
    libs = sorted(set(re.findall(r"[\w./+-]+\.a\b", mp)))
    lines.append("  objects named in the map: " + (", ".join(linked) if linked else "(none parsed)"))
    lines.append("  archives named in the map: " + (", ".join(libs) if libs else "(none)"))
    for b in BANNED:
        lines.append(f"  {b!r} in map or symbol table: {'YES' if b in blob else 'no'}")
    rt_load, data_load = sym_addr(sym, "_rt_load"), sym_addr(sym, "_data_load")
    rt0, rt1 = sym_addr(sym, "_rt_start"), sym_addr(sym, "_rt_end")
    d0, d1 = sym_addr(sym, "_data_start"), sym_addr(sym, "_data_end")
    etext = sym_addr(sym, "_etext")
    if None not in (rt_load, data_load, rt0, rt1, d0, d1, etext):
        text_n, rt_n, data_n = etext - APP_XIP, rt1 - rt0, d1 - d0
        gap_rt = rt_load - etext
        gap_data = data_load - (rt_load + rt_n)
        span = (data_load + data_n) - APP_XIP
        lines.append("Load span of the linked image (symbol LMAs, not the JLFS entry):")
        lines.append(f"  .text    {text_n} B  LMA {APP_XIP:#x}..{etext:#x}")
        lines.append(f"  gap before .ram_text load   {gap_rt} B")
        lines.append(f"  .ram_text {rt_n} B  LMA {rt_load:#x}  VMA {rt0:#x}")
        lines.append(f"  gap before .data load        {gap_data} B")
        lines.append(f"  .data    {data_n} B  LMA {data_load:#x}  VMA {d0:#x}")
        lines.append(f"  LMA span {span} B; volt.bin {len(app)} B; difference {len(app) - span} B (build.py word-align pad)")
        lines.append("  0xFF bytes inside volt.bin are inside that span. They are used, not spare.")
    lines.append("  stub symbols in the symbol table:")
    for s in STUBS:
        lines.append(f"    {s}: {'present' if re.search(r'\b' + s + r'\b', sym) else 'ABSENT'}")
    src_hits = []
    for p in SRC.rglob("*"):
        if not p.is_file() or any(x in p.parts for x in (".git", "build", "__pycache__")):
            continue
        if p.suffix.lower() not in {".c", ".h", ".S", ".s", ".ld", ".py", ".md", ".txt"}:
            continue
        t = p.read_text(errors="replace")
        for b in ("libfm1_engine", "fm_core_render", "dx7note_compute"):
            if b in t:
                src_hits.append(f"{p.relative_to(SRC)}: {b}")
    lines.append("  source hits for stock engine names: " + (", ".join(src_hits) if src_hits else "none"))
    dinghy_c = SRC / "firmware" / "src" / "dinghy.c"
    lines.append(f"  firmware/src/dinghy.c present: {dinghy_c.exists()}")
    logo = [str(p.relative_to(SRC)) for p in (SRC / "assets").rglob("*") if p.is_file() and "logo" in p.name.lower()] if (SRC / "assets").exists() else []
    lines.append("  logo files under assets/: " + (", ".join(logo) if logo else "none"))

    text = "\n".join(lines) + "\n"
    out = SRC / "build" / "flash_account.txt"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print(text, end="")
    print(f"wrote {out}")
    return 0 if verified else 2


def os_toolchain():
    import os
    tc = os.environ.get("JIELI_TOOLCHAIN", str(Path.home() / ".jieli" / "toolchain"))
    return tc


if __name__ == "__main__":
    sys.exit(main())
