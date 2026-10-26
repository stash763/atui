#!/usr/bin/env python3
"""Read a Watcom execution-sampler trace and say where the time went.

`wsample` (Open Watcom's sampling profiler) stops a traced child every few
milliseconds, reads each interrupted thread's instruction pointer, and writes
the addresses to a `.smp` file.  The viewer Watcom ships for those files is a
GUI executable, which is no use over a telnet bridge to a machine whose screen
is a text mode console -- so this reads the same file, resolves the addresses
against the wlink `.map` of the profiled program, and prints a flat profile:

    samples per function, per thread, and per module.

The module line matters as much as the function line on this target.  A 32-bit
OS/2 program reaches the kernel through thunks that live in DLLs (DosSleep and
the VIO/Kbd/Mou calls in DOSCALLS, select/recv in TCPIP32), so a thread that is
burning the machine inside the transport shows up as samples in a DLL, and the
profile is the only instrument that can say so.

File format: `bld/wsample/h/sample.h` in the Open Watcom sources.  Everything
is little-endian and packed:

    text "WATCOM Profiler data file V..." + NUL        (file starts here)
    blocks...                                         (file offset sample_start)
    SAMP_LAST block (a bare prefix)
    samp_header { sig 0xDEED, major 2, minor, sample_start }   (file ends here)

Each block: prefix { u32 tick, u16 length (whole block), u16 kind }, then data
for that kind.  INFO comes first and is rewritten in place when the run ends,
so its counts are final.  ADDR_MAP blocks carry exactly one translation each --
map segment N lives at linear address `actual.offset` -- and follow the
CODE_LOAD/MAIN_LOAD block of the module they describe.  SAMPLES blocks carry
one thread id and then (u32 offset, u16 segment) pairs; on a flat OS/2 program
the offset *is* the linear address and the segment is the code selector.

Usage:

    python3 tools/wsample_report.py --smp cand7.smp --map candidate7.map
    python3 tools/wsample_report.py --self-test
"""

import argparse
import bisect
import os
import struct
import sys
import tempfile

# ---------------------------------------------------------------------------
# The trace, as bytes.


class Block(object):
    """One block of a sample file, already unpacked."""

    def __init__(self, tick, kind, data, offset):
        self.tick = tick
        self.kind = kind
        self.data = data
        self.offset = offset        # where the block starts in the file


KIND_NAMES = [
    "INFO", "SAMPLES", "MARK", "OVL_LOAD", "CODE_LOAD",
    "ADDR_MAP", "MAIN_LOAD", "LAST", "REMAP_SECTION", "CALLGRAPH",
]
KIND_INFO, KIND_SAMPLES, KIND_MARK = 0, 1, 2
KIND_CODE_LOAD, KIND_ADDR_MAP, KIND_MAIN_LOAD = 4, 5, 6
KIND_LAST = 7

SAMP_SIGNATURE = 0xDEED


def parse_smp(path):
    """Return (info, modules, samples, marks) from a .smp file.

    `modules` is the chronological list of module loads, each with the map
    segment -> linear base table that followed it.  `samples` is a list of
    (thread_id, linear_address) in the order the blocks were flushed.
    """
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) < 8:
        die("%s: %d bytes is not a sample file" % (path, len(blob)))

    # samp_header is packed: 2+1+1+4 = 8 bytes, not the 12 an aligned
    # struct would occupy.  Reading 12 back finds the LAST block's prefix
    # instead and every signature looks wrong.
    sig, major, minor, sample_start = struct.unpack_from("<HBBI", blob, len(blob) - 8)
    if sig != SAMP_SIGNATURE:
        die("%s: signature 0x%04x is not a Watcom sample file" % (path, sig))
    if major != 2:
        die("%s: sample format version %d is not one this reader knows"
            % (path, major))

    blocks = []
    pos = sample_start
    while pos + 8 <= len(blob) - 8:
        tick, length, kind = struct.unpack_from("<IHH", blob, pos)
        if length < 8 or pos + length > len(blob) - 8:
            die("%s: block at %d claims %d bytes, past the header"
                % (path, pos, length))
        blocks.append(Block(tick, kind, blob[pos + 8:pos + length], pos))
        pos += length
    if pos != len(blob) - 8:
        die("%s: blocks end %d bytes short of the header" % (path, len(blob) - 8 - pos))

    info = {}
    modules = []       # [ {name, kind, bases: {map seg: linear}} ]
    samples = []       # [ (tid, linear) ]
    marks = []         # [ (tid, linear, text) ]
    for blk in blocks:
        if blk.kind == KIND_INFO:
            rate_us, = struct.unpack_from("<I", blk.data, 0)
            info["rate_ms"] = rate_us / 1000.0
        elif blk.kind == KIND_SAMPLES:
            if len(blk.data) < 2:
                continue
            tid, = struct.unpack_from("<H", blk.data, 0)
            n = (len(blk.data) - 2) // 6
            for i in range(n):
                off, = struct.unpack_from("<I", blk.data, 2 + i * 6)
                samples.append((tid, off))
        elif blk.kind == KIND_MARK:
            if len(blk.data) < 8:
                continue
            off, = struct.unpack_from("<I", blk.data, 0)
            tid, = struct.unpack_from("<H", blk.data, 6)
            marks.append((tid, off, blk.data[8:].split(b"\0")[0].decode(
                "latin-1", "replace")))
        elif blk.kind in (KIND_CODE_LOAD, KIND_MAIN_LOAD):
            if len(blk.data) < 11:
                continue
            name = blk.data[10:].split(b"\0")[0].decode("latin-1", "replace")
            modules.append({"name": name, "kind": blk.kind, "bases": {}})
        elif blk.kind == KIND_ADDR_MAP:
            if len(blk.data) < 12 or not modules:
                continue
            # mapping { map: {u32 offset, u16 segment},
            #             actual: {u32 offset, u16 segment} }
            # map.offset is always 0; map.segment is the map-file segment
            # number; actual.offset is where that segment was loaded.
            map_seg, = struct.unpack_from("<H", blk.data, 4)
            actual_off, = struct.unpack_from("<I", blk.data, 6)
            modules[-1]["bases"][map_seg] = actual_off
    if "rate_ms" not in info:
        die("%s: no INFO block; the run never wrote its header" % path)
    return info, modules, samples, marks


# ---------------------------------------------------------------------------
# The map, as symbols.


class WlinkMap(object):
    """A wlink `op m` map: segments with classes, and symbols by segment."""

    def __init__(self, path):
        self.segments = {}      # seg number -> {class, size}
        self.symbols = []       # [ (seg, offset, name, flags) ]
        self._parse(path)

    @staticmethod
    def _split(line):
        return line.rstrip("\n").split()

    def _parse(self, path):
        where = None
        with open(path, "r", errors="replace") as f:
            for line in f:
                fields = self._split(line)
                if not fields:
                    continue
                if line.startswith("Module:"):
                    where = line[len("Module:"):].strip()
                    continue
                # Segment table rows: NAME CLASS GROUP ADDR SIZE
                if (len(fields) == 5 and ":" in fields[3]
                        and self._is_addr(fields[3]) and self._is_hex8(fields[4])
                        and not line.startswith(" ")):
                    self._note_segment(fields)
                    continue
                # Symbol rows: ADDR[flags] NAME, under a Module: header.  The
                # wlink flags (* unreferenced, + local, s static) stick to the
                # address with no space: `0002:000007a0s Sleeper`.
                if where is not None and len(fields) >= 2 \
                        and self._is_sym_addr(fields[0]):
                    self._note_symbol(fields, where)
        if not self.symbols:
            die("%s: no symbols recognised; is this a wlink map?" % path)

    @staticmethod
    def _is_addr(text):
        if len(text) < 6 or ":" not in text:
            return False
        seg, _, off = text.partition(":")
        return seg.isdigit() and len(off) == 8 and all(
            c in "0123456789abcdef" for c in off)

    @staticmethod
    def _is_hex8(text):
        # Segment sizes in the segment table are bare hex: `00000068`.
        return len(text) == 8 and all(c in "0123456789abcdef" for c in text)

    @staticmethod
    def _is_sym_addr(text):
        # `0002:000007a0`, `0002:000007a0s`, `0002:000007a0*`, `0002:000007a0+`
        if text and text[-1] in "*+s":
            text = text[:-1]
        return WlinkMap._is_addr(text)

    def _note_segment(self, fields):
        seg_text = fields[3].partition(":")[0]
        try:
            seg = int(seg_text)
            size = int(fields[4], 16)
        except ValueError:
            return
        self.segments[seg] = {"class": fields[1], "size": size}

    def _note_symbol(self, fields, module):
        addr = fields[0]
        flags = ""
        if addr and addr[-1] in "*+s":
            addr, flags = addr[:-1], addr[-1]
        # Demangled C++ rows read `TYPE near qualified::name( args )`; the
        # name is after the first ` near `.  C symbols are the bare rest.
        rest = " ".join(fields[1:])
        # The OW demangler marks every far-pointer parameter with ` near `,
        # inside templates too; for a label it is noise, and the return type
        # before the name is noise as well.  Drop both, keep the qualified
        # name; take the head when arguments follow.
        rest = rest.replace(" near ", " ")
        name = rest.split("(", 1)[0].strip()
        if " " in name:                       # `TYPE name` -- keep the name
            name = name.rsplit(" ", 1)[1] if "(" not in name else name
        seg_text, _, off_text = addr.partition(":")
        try:
            seg = int(seg_text)
            off = int(off_text, 16)
        except ValueError:
            return
        self.symbols.append((seg, off, name, flags, module))


def symbol_table(wmap, bases):
    """(linear address, name) for every symbol, sorted; map segments resolved
    against the ADDR_MAP bases of the module the map describes."""
    code_segs = set(seg for seg, s in wmap.segments.items()
                    if s["class"] in ("CODE",))
    out = []
    for seg, off, name, flags, module in wmap.symbols:
        if seg not in bases:
            continue
        linear = bases[seg] + off
        out.append((linear, name, seg in code_segs, module))
    out.sort(key=lambda t: t[0])
    return out


# ---------------------------------------------------------------------------
# Resolution.


def resolve(samples, modules, table, main_name):
    """Turn (tid, linear) into (tid, module, label) rows."""
    # Each module's address span: from its lowest base to the next module's
    # lowest base (DLLs have no sizes here; the map has sizes for the main
    # image only, and the next module's base is an honest upper bound on a
    # machine with no ASLR).
    spans = []
    for mod in modules:
        if not mod["bases"]:
            continue
        spans.append((min(mod["bases"].values()), mod["name"]))
    spans.sort()

    main = None
    for mod in modules:
        if mod["kind"] == KIND_MAIN_LOAD or mod["name"] == main_name:
            main = mod
            break
    code_linear = sorted((a, n) for a, n, is_code, m in table if is_code) \
        if table else []
    starts = [a for a, n in code_linear]

    rows = []
    for tid, addr in samples:
        # Which module?  The last one based at or below the address.
        name = None
        for base, modname in spans:
            if addr >= base:
                name = modname
            else:
                break
        label = None
        if main is not None and name == main["name"] and code_linear:
            i = bisect.bisect_right(starts, addr) - 1
            if i >= 0:
                label = code_linear[i][1]
        rows.append((tid, name, label, addr))
    return rows, main


def module_end(rows, main, wmap, bases):
    """The main image's highest code address, so 'main image' attribution can
    be checked against the next module's base."""
    if main is None or wmap is None:
        return None
    end = 0
    for seg, meta in wmap.segments.items():
        if seg in bases and meta["class"] == "CODE":
            end = max(end, bases[seg] + meta["size"])
    return end


# ---------------------------------------------------------------------------
# The report.


def report(info, modules, rows, marks, main, args):
    total = len(rows)
    if total == 0:
        print("no samples in the trace")
        return 1

    per_thread = {}
    per_module = {}         # module -> [count, distinct addresses, set(tids)]
    per_symbol = {}         # (module, label) -> [count, {tid: count}]
    for tid, name, label, addr in rows:
        t = per_thread.setdefault(tid, 0)
        per_thread[tid] = t + 1
        name = name if name is not None else "<below every module>"
        mod = per_module.setdefault(name, [0, set(), set()])
        mod[0] += 1
        mod[1].add(addr)
        mod[2].add(tid)
        sym = per_symbol.setdefault((name, label), [0, {}])
        sym[0] += 1
        sym[1][tid] = sym[1].get(tid, 0) + 1

    print("sample file: %s" % args.smp)
    print("sampling interval: %.0f ms" % info["rate_ms"])
    est = total * info["rate_ms"] / 1000.0
    print("samples: %d  (~%.0f s of run)" % (total, est))
    print()
    print("modules loaded (in load order):")
    for mod in modules:
        print("  %-28s map segments at %s" % (
            base_name(mod["name"]),
            " ".join("%d=0x%x" % (s, b) for s, b in sorted(mod["bases"].items()))
            or "(no addresses)"))
    print()
    print("by thread:")
    for tid in sorted(per_thread):
        print("  thread %-3d %6d  %5.1f%%" % (
            tid, per_thread[tid], 100.0 * per_thread[tid] / total))
    print()
    print("by module (distinct addresses: a handful is a thread parked in one")
    print("call; spread wide is a loop actually running):")
    mods = sorted(per_module.items(), key=lambda kv: -kv[1][0])
    for name, (count, addrs, tids) in mods:
        print("  %-28s %6d  %5.1f%%  %4d addrs  threads %s" % (
            base_name(name), count, 100.0 * count / total,
            len(addrs), ",".join(str(t) for t in sorted(tids))))
    print()
    print("top functions:")
    syms = sorted(per_symbol.items(), key=lambda kv: -kv[1][0])[:args.top]
    for (name, label), (count, tids) in syms:
        by_tid = " ".join("t%d:%d" % (t, tids[t]) for t in sorted(tids))
        print("  %6d  %5.1f%%  %-24s %s" % (
            count, 100.0 * count / total,
            label if label is not None else "(no symbol)",
            by_tid))
    if marks:
        print()
        print("marks:")
        for tid, addr, text in marks:
            print("  thread %d at 0x%x: %s" % (tid, addr, text))
    return 0


# ---------------------------------------------------------------------------
# The trace, as prof wrote it (tools/os2/prof.c): plain text, one event a
# line.  prof exists because wsample cannot watch a multithreaded OS/2 program
# without killing it; the format is one line per event, deliberately simple
# enough to read with a pager on the box itself.
#
#   RATE <ms> / BUDGET <ms>
#   RUN <pid> <program> <hh:mm:ss>
#   NEWPROC <eip-hex> <mte-hex>
#   MODULE <mte-hex> <name>
#   THREAD <tid> / THREADEND <tid>
#   SAMPLE <tid> <eip-hex>
#   EXC <state> <number-hex> <fault-hex> <eip-hex> <tid>
#   OBJSCAN <seq> / OBJ <num> <addr-hex> <mte-hex> <index>
#   STATS exceptions <n> firstchance <n> <hh:mm:ss>
#   END <reason>


def parse_trc(path):
    """Return (info, modules, obj_addrs, samples, excs, threads) from a .trc."""
    info = {"rate_ms": None, "budget_ms": 0}
    modules = []        # [(mte, name)] in load order
    obj_addrs = {}      # object number -> linear base (first OBJSCAN wins)
    samples = []        # [(tid, linear)]
    excs = []           # [(state, number, fault, eip, tid)]
    threads = []        # (tid, "up"|"down") events in order
    in_first_scan = True
    with open(path, "r", errors="replace") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            k = p[0]
            try:
                if k == "RATE":
                    info["rate_ms"] = float(p[1])
                elif k == "BUDGET":
                    info["budget_ms"] = int(p[1])
                elif k == "RUN":
                    info["pid"] = int(p[1])
                    info["program"] = p[2]
                elif k == "OBJSCAN":
                    in_first_scan = int(p[1]) == 0
                elif k == "OBJ" and in_first_scan and len(p) >= 3:
                    obj_addrs[int(p[1])] = int(p[2], 16)
                elif k == "MODULE" and len(p) >= 3:
                    modules.append((p[1], " ".join(p[2:])))
                elif k == "SAMPLE" and len(p) >= 3:
                    samples.append((int(p[1]), int(p[2], 16)))
                elif k == "EXC" and len(p) >= 6:
                    excs.append((int(p[1]), int(p[2], 16), int(p[3], 16),
                                 int(p[4], 16), int(p[5])))
                elif k == "THREAD" and len(p) >= 2:
                    threads.append((int(p[1]), "up"))
                elif k == "THREADEND" and len(p) >= 2:
                    threads.append((int(p[1]), "down"))
                elif k == "STATS":
                    info["stats"] = " ".join(p[1:])
                elif k == "END":
                    info["end"] = " ".join(p[1:])
            except (ValueError, IndexError):
                continue
    if not info["rate_ms"]:
        die("%s: no RATE line; the run never wrote its header" % path)
    return info, modules, obj_addrs, samples, excs, threads


def trc_code_base(obj_addrs, wmap, samples):
    """Where the loader put the map's code.

    prof's object table gives the linear bases of the image's objects, but
    not which one holds _TEXT -- and the order is not the map's (measured on
    the box: candidate7's code loaded at 0x20000 with a stub object first at
    0x10000, probe_busy's code at 0x10000).  So ask the samples: try every
    object base as the base of the code segment group and keep the one that
    puts the most samples inside it.  The map's code segment addresses are
    relative to the group's base, which is exactly where an object starts.
    """
    if not obj_addrs or not samples:
        return None, None
    code_segs = set(seg for seg, meta in wmap.segments.items()
                    if meta["class"] == "CODE")
    code_syms = sorted(off for seg, off, _n, _f, _m in wmap.symbols
                       if seg in code_segs)
    if not code_syms:
        return None, None
    span = code_syms[-1] + 16
    best, best_hits = None, -1
    for base in sorted(set(obj_addrs.values())):
        lo, hi = base + code_syms[0], base + span
        hits = sum(1 for _t, a in samples if lo <= a < hi)
        if hits > best_hits:
            best, best_hits = base, hits
    if best is None or best_hits == 0:
        return None, None
    table = sorted((best + off, name)
                   for seg, off, name, _f, _m in wmap.symbols
                   if seg in code_segs)
    return best, table


def trc_report(info, modules, obj_addrs, samples, excs, threads, wmap, args):
    total = len(samples)
    if total == 0:
        print("no samples in the trace")
        return 1

    code_base, code_table = (None, None)
    if wmap is not None:
        code_base, code_table = trc_code_base(obj_addrs, wmap, samples)
        if code_base is not None:
            starts = [a for a, _n in code_table]

    def label_of(addr):
        if code_table is None:
            return None
        i = bisect.bisect_right(starts, addr) - 1
        return code_table[i][1] if i >= 0 else None

    low_ceiling = min(obj_addrs.values()) if obj_addrs else 0x10000
    hi_floor = max(obj_addrs.values()) if obj_addrs else 0
    if code_base is not None:
        hi_floor = max(hi_floor, code_base)
    # Above the highest known object the loader put something we cannot name
    # without that module's own map -- system DLLs live up there (and the
    # kernel's own areas live up there too).  Split only to keep the report
    # honest: `0x7fff0000` is a DLL wait, not user code.
    threads_seen = {}
    for tid, _ in samples:
        threads_seen[tid] = threads_seen.get(tid, 0) + 1
    up = sorted(set(t for t, what in threads if what == "up"))

    print("trace file:  %s" % args.trc)
    print("program:     %s (pid %s)" % (info.get("program", "?"),
                                        info.get("pid", "?")))
    print("sampling:    %g ms interval, budget %d ms, end %s"
          % (info["rate_ms"], info["budget_ms"], info.get("end", "?")))
    est = total * info["rate_ms"] / 1000.0
    print("samples:     %d  (~%.0f s of stopped sampling)" % (total, est))
    if code_base is not None:
        print("code at:     0x%x (from the map; %d symbols)"
              % (code_base, len(code_table)))
    else:
        print("code at:     unknown (%s)"
              % ("no --map" if wmap is None else "no samples matched an object"))
    if threads:
        print("threads:     %s" % ", ".join(
            "tid %d (%s)" % (t, "up" if what == "up" else "ended")
            for t, what in threads))
    print()

    print("by thread:")
    for tid in sorted(threads_seen):
        rows = [a for t, a in samples if t == tid]
        parked = most_common_addr(rows)
        if code_table is not None and \
                code_table[0][0] <= parked[0] <= code_table[-1][0] + 4096:
            hot = "in-image function " + str(label_of(parked[0]))
        elif parked[0] < low_ceiling:
            hot = "kernel wait (DosSleep path)"
        else:
            hot = "above the image (DLL or kernel)"
        print("  tid %-3d %6d  %5.1f%%   hottest address 0x%x x%d (%s)"
              % (tid, threads_seen[tid], 100.0 * threads_seen[tid] / total,
                 parked[0], parked[1], hot))
    print()

    # What the threads were doing: symbolized where the map can say, by
    # address region where it cannot.
    counts = {}
    for tid, addr in samples:
        if code_table is not None and \
                code_table[0][0] <= addr <= code_table[-1][0] + 4096:
            key = ("image", label_of(addr))
        elif addr < low_ceiling:
            key = ("below image (kernel shared -- DosSleep and friends)", None)
        else:
            key = ("above image (DLLs: TCP/IP, DOSCALLS ...)", None)
        c = counts.setdefault(key, [0, {}])
        c[0] += 1
        c[1][tid] = c[1].get(tid, 0) + 1
    print("what the samples were doing (top %d):" % args.top)
    for (region, label), (count, tids) in sorted(
            counts.items(), key=lambda kv: -kv[1][0])[:args.top]:
        by_tid = " ".join("t%d:%d" % (t, tids[t]) for t in sorted(tids))
        what = label if label else region
        print("  %6d  %5.1f%%  %-58s %s" % (
            count, 100.0 * count / total, what, by_tid))
    if excs:
        print()
        print("exceptions (state 0=pre-first 1=first 2=last 3=stack-invalid):")
        for state, num, fault, eip, tid in excs:
            print("  tid %d state %d number 0x%08x fault 0x%x eip 0x%x"
                  % (tid, state, num, fault, eip))
    if info.get("stats"):
        print()
        print("prof's stats: %s" % info["stats"])
    return 0


def most_common_addr(rows):
    c = {}
    for a in rows:
        c[a] = c.get(a, 0) + 1
    best = (0, 0)
    for a, n in c.items():
        if n > best[1]:
            best = (a, n)
    return best


# ---------------------------------------------------------------------------
# Self-test: write a trace the way wsample writes one, then read it back.


def base_name(path):
    """The file part of a path, whichever way the target writes its slashes."""
    if path is None:
        return "<none>"
    return path.replace("\\", "/").rsplit("/", 1)[-1]


def die(message):
    sys.stderr.write("wsample_report: %s\n" % message)
    raise SystemExit(2)


def build_test_smp(path, bases, samples, main_map_name, dll_base):
    """Emit a sample file exactly as c/sample.c writes it."""
    def block(kind, payload, tick=0):
        return struct.pack("<IHH", tick, 8 + len(payload), kind) + payload

    out = bytearray()
    prefix = b"WATCOM Profiler data file V2.2\x1a\x00"
    out += prefix
    sample_start = len(out)

    info_payload = struct.pack("<I", 20000) + b"\x00" * 8 + b"\x00" * (13 * 8)
    info_block = block(KIND_INFO, info_payload)
    out += info_block

    out += block(KIND_MAIN_LOAD, struct.pack("<I", 0) + struct.pack("<IH", 0, 0)
                 + main_map_name.encode("latin-1") + b"\0")
    for seg, base in sorted(bases.items()):
        out += block(KIND_ADDR_MAP,
                     struct.pack("<IH", 0, seg) + struct.pack("<IH", base, 0x73))
    # A DLL loaded above the main image, the way TCPIP32 lands above atui.
    out += block(KIND_CODE_LOAD, struct.pack("<I", 0) + struct.pack("<IH", 0, 0)
                 + b"D:\\DLL\\FAKE32.DLL\0")
    out += block(KIND_ADDR_MAP,
                 struct.pack("<IH", 0, 1) + struct.pack("<IH", dll_base, 0x73))

    by_thread = {}
    for tid, addr in samples:
        by_thread.setdefault(tid, []).append(addr)
    for tid in sorted(by_thread):
        payload = struct.pack("<H", tid)
        for addr in by_thread[tid]:
            payload += struct.pack("<IH", addr, 0x73)
        out += block(KIND_SAMPLES, payload, tick=len(by_thread[tid]))

    out += block(KIND_LAST, b"")
    out += struct.pack("<HBBI", SAMP_SIGNATURE, 2, 2, sample_start)
    # wsample rewrites the INFO block in place; the counts are not needed by
    # this reader, so the placeholder version above is what it left.

    with open(path, "wb") as f:
        f.write(bytes(out))
    return sample_start


def self_test():
    """A trace written by this file's own writer, symbolicated against a real
    wlink map from the Open Watcom tree, must name the functions the samples
    were placed in."""
    ow_map = None
    for candidate in (
        os.path.expanduser(
            "~/code/open-watcom-v2/bld/wsample/os2386/wsample.map"),
    ):
        if os.path.exists(candidate):
            ow_map = candidate
            break
    if ow_map is None:
        die("self-test needs the Open Watcom tree's own wsample.map")

    wmap = WlinkMap(ow_map)
    # The map's own segments: 1 = _TEXT16, 2 = _TEXT (the code), 3 = DGROUP.
    bases = {1: 0x10000, 2: 0x10068, 3: 0x20000}
    table = symbol_table(wmap, bases)
    names = dict((name, linear) for linear, name, is_code, module in table)

    def at(name):
        if name not in names:
            die("self-test: %s is not in %s" % (name, ow_map))
        return names[name]

    main_name = "D:\\ATUI\\WSAMPLE.EXE"
    dll_base = 0x30000
    samples = [
        # Thread 1: eight samples inside Sleeper_, four inside StartProg_.
        (1, at("Sleeper")) for _ in range(8)
    ] + [
        (1, at("StartProg_")) for _ in range(4)
    ] + [
        # Thread 2: parked in one DLL address, and one below every module.
        (2, dll_base + 0x40) for _ in range(3)
    ] + [
        (2, 0x1000)
    ]

    tmp = tempfile.mkdtemp(prefix="wsample-report-")
    smp = os.path.join(tmp, "test.smp")
    build_test_smp(smp, bases, samples, main_name, dll_base)

    info, modules, readback, marks = parse_smp(smp)
    rows, main = resolve(readback, modules, table, main_name)
    got = {}
    for tid, name, label, addr in rows:
        key = (tid, label, base_name(name))
        got[key] = got.get(key, 0) + 1

    def expect(key, want):
        have = got.get(key, 0)
        if have != want:
            die("self-test: %s counted %d, wanted %d (got %r)"
                % (key, have, want, got))

    expect((1, "Sleeper", "WSAMPLE.EXE"), 8)
    expect((1, "StartProg_", "WSAMPLE.EXE"), 4)
    expect((2, None, "FAKE32.DLL"), 3)
    expect((2, None, "<none>"), 1)
    if main is None or "WSAMPLE.EXE" not in main["name"]:
        die("self-test: main module not identified (got %r)" % (main,))
    if info["rate_ms"] != 20.0:
        die("self-test: rate read as %r, wrote 20 ms" % info["rate_ms"])
    print("self-test: %d samples symbolicated correctly against %s"
          % (len(readback), os.path.basename(ow_map)))
    return 0


# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Report where a wsample trace spent its time.")
    ap.add_argument("--smp", help="the .smp file wsample wrote")
    ap.add_argument("--trc", help="the .trc text trace prof wrote "
                                  "(tools/os2/prof.c)")
    ap.add_argument("--map", help="the wlink map of the profiled program")
    ap.add_argument("--top", type=int, default=25,
                    help="how many functions to list (default 25)")
    ap.add_argument("--self-test", action="store_true",
                    help="write a synthetic trace, read it back, check it")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    if args.trc:
        info, modules, obj_addrs, samples, excs, threads = parse_trc(args.trc)
        wmap = WlinkMap(args.map) if args.map else None
        return trc_report(info, modules, obj_addrs, samples, excs, threads,
                          wmap, args)
    if not args.smp or not args.map:
        ap.error("--smp and --map are required (or --trc, or --self-test)")

    info, modules, samples, marks = parse_smp(args.smp)
    wmap = WlinkMap(args.map)
    main_mod = None
    for mod in modules:
        if mod["kind"] == KIND_MAIN_LOAD:
            main_mod = mod
            break
    if main_mod is None:
        # Older traces name the main image in a MAIN_LOAD block; if the run
        # only wrote CODE_LOAD blocks, the first one is the main image.
        main_mod = modules[0] if modules else None
    if main_mod is None or not main_mod["bases"]:
        die("the trace names no module with addresses; nothing to resolve against")
    table = symbol_table(wmap, main_mod["bases"])
    rows, main = resolve(samples, modules, table, main_mod["name"])
    return report(info, modules, rows, marks, main, args)


if __name__ == "__main__":
    raise SystemExit(main())