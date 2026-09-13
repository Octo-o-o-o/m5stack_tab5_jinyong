#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Flood-fill the starting submap with SubMap::tryMove's exact blocking rule."""
import struct, sys, pathlib
from collections import deque

ROOT = pathlib.Path(sys.argv[1]); DATA = ROOT/"data"
W=H=64; LAYERS=6

def grp_records(stem, grp_suffix=".GRP", idx_suffix=".IDX"):
    idx=(DATA/f"{stem}{idx_suffix}").read_bytes(); grp=(DATA/f"{stem}{grp_suffix}").read_bytes()
    ends=struct.unpack(f"<{len(idx)//4}I", idx); out=[]; off=0
    for e in ends:
        if e==0: e=len(grp)
        out.append(grp[off:e]); off=e
    return out

raw=(DATA/"Z.DAT").read_bytes()
print("Z.DAT size = 0x%X (%d)" % (len(raw), len(raw)))
o = dict(id=0x26d6e,x=0x26db7,y=0x26dc0) if len(raw)==0x5F000 else dict(id=0x2076e,x=0x207b7,y=0x207c0)
sid,sx,sy=[struct.unpack_from("<h",raw,o[k])[0] for k in ("id","x","y")]

rec=grp_records("ALLSIN")[sid]
lay=[struct.unpack_from(f"<{W*H}h", rec, l*W*H*2) for l in range(LAYERS)]

# SubMapEvent = 11 int16 = 22 bytes; SubMapEventData = 200 events
ev_rec=grp_records("ALLDEF")[sid]
EV=22; NEV=200
events=[struct.unpack_from("<11h", ev_rec, i*EV) for i in range(min(NEV, len(ev_rec)//EV))]
# fields: blocked,index,event0,event1,event2,currTex,endTex,begTex,texDelay,x,y

idx=(DATA/"SDX").read_bytes(); ends=struct.unpack(f"<{len(idx)//4}I", idx)
smpsize=(DATA/"SMP").stat().st_size
empt=[]; off=0
for e in ends:
    if e==0: e=smpsize
    empt.append(e-off==0); off=e

def cell_blocked(x,y):
    pos=y*W+x
    t=lay[0][pos]>>1
    b=(179<=t<=181) or t==261 or t==511 or (662<=t<=665) or t==674
    bid=lay[1][pos]>>1
    if bid>=0 and (bid>=len(empt) or empt[bid]): b=True
    if bid: return True, "building"
    if b: return True, "terrain"
    e=lay[3][pos]
    if 0<=e<len(events) and events[e][0]: return True, f"event{e}"
    return False, ""

seen={(sx,sy)}; q=deque([(sx,sy)])
while q:
    x,y=q.popleft()
    for dx,dy in ((0,-1),(0,1),(-1,0),(1,0)):
        nx,ny=x+dx,y+dy
        if not(0<=nx<W and 0<=ny<H) or (nx,ny) in seen: continue
        blk,_=cell_blocked(nx,ny)
        if blk: continue
        seen.add((nx,ny)); q.append((nx,ny))

print(f"start=({sx},{sy})  reachable cells={len(seen)}")
sm=grp_records("RANGER")[3]
b=sm[sid*52:(sid+1)*52]; f=struct.unpack_from("<20h", b, 12)
exits=[(f[10+i], f[13+i]) for i in range(3)]
enter=(f[8],f[9])
print(f"enter=({enter[0]},{enter[1]}) reachable={enter in seen}  blocked={cell_blocked(*enter)}")
for i,(ex,ey) in enumerate(exits):
    print(f"exit[{i}]=({ex},{ey}) reachable={(ex,ey) in seen}  cellcheck={cell_blocked(ex,ey)}")

xs=[p[0] for p in seen]; ys=[p[1] for p in seen]
print(f"reachable bbox x {min(xs)}..{max(xs)}  y {min(ys)}..{max(ys)}")

# --- emit a BFS path start -> exit[0] as U/D/L/R for the host walk test ---
import collections, os
tgt = exits[0]
prev = {(sx,sy): None}
q = deque([(sx,sy)])
while q:
    x,y = q.popleft()
    if (x,y) == tgt: break
    for dx,dy,ch in ((0,-1,'U'),(0,1,'D'),(-1,0,'L'),(1,0,'R')):
        nx,ny = x+dx, y+dy
        if not(0<=nx<W and 0<=ny<H) or (nx,ny) in prev: continue
        if cell_blocked(nx,ny)[0]: continue
        prev[(nx,ny)] = (x,y,ch); q.append((nx,ny))
path=[]; cur=tgt
while prev.get(cur):
    px,py,ch = prev[cur]; path.append(ch); cur=(px,py)
path.reverse()
out = os.environ.get("PATH_OUT", "")
print(f"path start->exit: {len(path)} steps: {''.join(path)}")
if out:
    open(out,"w").write("".join(path))
    print("written to", out)
