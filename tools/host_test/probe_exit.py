#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Replay SubMap::load + SubMap::tryMove exit checks against the real card data.

Layouts taken from the sources this firmware compiles:
  Z.DAT offsets      -> content/factors.cc
  SubMapData(52B)    -> world/submap.hh   (RANGER.GRP record 3)
  SubMapLayerData    -> 6 layers x 64x64 int16 (ALLSIN.GRP record <submap id>)
  blocked rule       -> scene/submap.cc SubMap::load
"""
import struct, sys, pathlib

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "local/sd_image/jinyong")
DATA = ROOT / "data"
W = H = 64
LAYERS = 6

def grp_records(stem):
    idx = (DATA / f"{stem}.IDX").read_bytes()
    grp = (DATA / f"{stem}.GRP").read_bytes()
    ends = struct.unpack(f"<{len(idx)//4}I", idx)
    out, off = [], 0
    for e in ends:
        if e == 0:
            e = len(grp)
        out.append(grp[off:e]); off = e
    return out

def factors():
    raw = (DATA / "Z.DAT").read_bytes()
    fish = 0x5F000
    if len(raw) == fish:
        o = dict(id=0x26d6e, x=0x26db7, y=0x26dc0, tex=0x26e2e); which="fishedit"
    else:
        o = dict(id=0x2076e, x=0x207b7, y=0x207c0, tex=0x2082e); which="legacy"
    g = lambda k: struct.unpack_from("<h", raw, o[k])[0]
    return which, g("id"), g("x"), g("y"), g("tex")

SUBMAP_FIELDS = ("id","name","exitMusic","enterMusic","switchSubMap","enterCondition",
                 "globalEnterX1","globalEnterY1","globalEnterX2","globalEnterY2",
                 "enterX","enterY","exitX0","exitX1","exitX2","exitY0","exitY1","exitY2",
                 "switchSubMapX","switchSubMapY","subMapEnterX","subMapEnterY")

def submaps():
    blob = grp_records("RANGER")[3]
    assert len(blob) % 52 == 0, len(blob)
    out = []
    for i in range(0, len(blob), 52):
        b = blob[i:i+52]
        vals = (struct.unpack_from("<h", b, 0)[0],
                b[2:12].split(b"\0",1)[0].decode("big5","ignore"))
        vals += struct.unpack_from("<20h", b, 12)
        out.append(dict(zip(SUBMAP_FIELDS, vals)))
    return out

def layers(sub_id):
    rec = grp_records("ALLSIN")[sub_id]
    need = LAYERS * W * H * 2
    assert len(rec) >= need, (len(rec), need)
    return [struct.unpack_from(f"<{W*H}h", rec, l*W*H*2) for l in range(LAYERS)]

def smp_empty():
    """texData_[i].empty() for the combined SDX/SMP set."""
    idx = (DATA / "SDX").read_bytes()
    ends = struct.unpack(f"<{len(idx)//4}I", idx)
    size = (DATA / "SMP").stat().st_size
    empt, off = [], 0
    for e in ends:
        if e == 0: e = size
        empt.append(e - off == 0); off = e
    return empt

def blocked_at(lay, empt, x, y):
    pos = y * W + x
    texId = lay[0][pos] >> 1
    blocked = (179 <= texId <= 181) or texId == 261 or texId == 511 \
              or (662 <= texId <= 665) or texId == 674
    bid = lay[1][pos] >> 1
    if bid >= 0 and (bid >= len(empt) or empt[bid]):
        blocked = True
    return texId, bid, blocked, lay[3][pos]

which, sid, sx, sy, stex = factors()
print(f"Z.DAT layout={which}  initSubMapId={sid} initSubMapX={sx} initSubMapY={sy} initMainCharTex={stex}")
sm = submaps()
print(f"RANGER submap entries={len(sm)}")
info = sm[sid]
print(f"\n== starting submap #{sid} name={info['name']!r} ==")
for k in ("exitMusic","enterMusic","switchSubMap","enterCondition","enterX","enterY",
          "exitX0","exitY0","exitX1","exitY1","exitX2","exitY2",
          "switchSubMapX","switchSubMapY","globalEnterX1","globalEnterY1"):
    print(f"  {k:16} = {info[k]}")

lay = layers(sid)
empt = smp_empty()
print(f"\nSMP records={len(empt)} empty={sum(empt)}")
print(f"\nstart cell ({sx},{sy}):", blocked_at(lay, empt, sx, sy))
print("\n== exit cells (SubMap::tryMove compares currX_/currY_ to these) ==")
for i in range(3):
    ex, ey = info[f"exitX{i}"], info[f"exitY{i}"]
    if ex < 0 or ey < 0 or ex >= W or ey >= H:
        print(f"  exit[{i}] = ({ex},{ey})  -> out of map, never reachable")
        continue
    texId, bid, blk, ev = blocked_at(lay, empt, ex, ey)
    verdict = "BLOCKED before the exit check" if (bid or blk) else "reachable"
    print(f"  exit[{i}] = ({ex},{ey})  earth={texId} building={bid} blocked={blk} eventIdx={ev}  -> {verdict}")
ex, ey = info["switchSubMapX"], info["switchSubMapY"]
if info["switchSubMap"] >= 0:
    texId, bid, blk, ev = blocked_at(lay, empt, ex, ey)
    print(f"  switchSubMap -> #{info['switchSubMap']} at ({ex},{ey}) building={bid} blocked={blk}")
