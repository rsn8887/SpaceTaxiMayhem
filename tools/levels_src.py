#!/usr/bin/env python3
"""Level source for Space Taxi Mayhem.  Run to validate and emit src/levels.inc

Map legend (40x27 tiles of 8x8 px):
  .  empty            #  metal wall          %  rock wall        =  girder
  1-9 landing pad     F  fuel pad            G  exit gate (top row)
  D  door, closed while switch is OFF        E  door, closed while switch is ON
  S  floor switch (land on it to toggle)     L / l  laser beam phase A / B
  ^ < > v  wind      T / t  teleporter pair  X  spikes
  |  -  background pipes (decoration, not solid)
"""
import sys, os

W, H = 40, 27
SOLID = set("#%=123456789FGDES")
CLEAR = set(".^<>v|-Tt")

class M:
    def __init__(s, name, theme, g, quota, gate=18):
        s.name, s.theme, s.g, s.quota = name, theme, g, quota
        s.t = [["." for _ in range(W)] for _ in range(H)]
        for x in range(W):
            s.t[0][x] = "#"; s.t[H - 1][x] = "#"
        for y in range(H):
            s.t[y][0] = "#"; s.t[y][W - 1] = "#"
        for x in range(gate, gate + 4):
            s.t[0][x] = "G"
        s.plats, s.mines = [], []

    def fill(s, x, y, w, h, c, only_empty=False):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if 0 <= xx < W and 0 <= yy < H:
                    if only_empty and s.t[yy][xx] != ".":
                        continue
                    s.t[yy][xx] = c
        return s

    def pad(s, x, y, w, n):
        return s.fill(x, y, w, 1, n)

    def ledge(s, x, y, w, n, c="#", depth=3):
        s.pad(x, y, w, n)
        s.fill(x - 1, y + 1, w + 2, 1, c)
        if depth >= 2: s.fill(x, y + 2, w, 1, c)
        if depth >= 3: s.fill(x + 1, y + 3, w - 2, 1, c)
        return s

    def gled(s, x, y, w, n):
        """pad on a girder with background support pipes down to the ground"""
        s.pad(x, y, w, n)
        s.fill(x - 1, y + 1, w + 2, 1, "=")
        for xx in (x, x + w - 1):
            yy = y + 2
            while yy < H - 1 and s.t[yy][xx] == ".":
                s.t[yy][xx] = "|"; yy += 1
        return s

    # x0,y0 -> x1,y1 tile coords of top-left, w tiles, period s, phase 0..1,
    # pad char or "", sw: 0 always moves, 1 only while switch ON, 2 only while OFF
    def plat(s, x0, y0, x1, y1, w, period, phase=0.0, pad="", sw=0):
        s.plats.append((x0, y0, x1, y1, w, period, phase, pad, sw)); return s

    # mine: linear patrol between tile centres, or circle (r>0) around x0,y0
    def mine(s, x0, y0, x1, y1, period, phase=0.0, r=0.0):
        s.mines.append((x0, y0, x1, y1, period, phase, r)); return s

    def rows(s):
        return ["".join(r) for r in s.t]


L = []

# 1 ---------------------------------------------------------------
m = M("FIRST FARE", 0, 1.0, 4)
m.ledge(4, 6, 4, "1").ledge(30, 6, 4, "2").ledge(15, 13, 4, "F")
m.ledge(6, 20, 4, "3").ledge(29, 20, 4, "4")
m.fill(1, 24, 38, 2, "%").fill(1, 22, 3, 2, "%").fill(36, 22, 3, 2, "%")
m.fill(12, 23, 4, 1, "%").fill(24, 23, 3, 1, "%")
m.fill(8, 1, 1, 4, "|").fill(31, 1, 1, 4, "|")
L.append(m)

# 2 ---------------------------------------------------------------
m = M("TWIN TOWERS", 1, 1.0, 5)
m.fill(14, 5, 4, 20, "%").fill(22, 5, 4, 20, "%")
m.pad(14, 4, 4, "1").pad(22, 4, 4, "2")
m.ledge(3, 9, 4, "3", "%").ledge(32, 9, 4, "4", "%")
m.ledge(8, 16, 4, "5", "%").ledge(28, 16, 4, "6", "%")
m.fill(1, 22, 7, 4, "%").pad(2, 21, 4, "F")
m.fill(33, 22, 6, 4, "%").fill(1, 24, 38, 2, "%")
L.append(m)

# 3 ---------------------------------------------------------------
m = M("THE SHAFT", 2, 1.0, 5)
for c in (17, 22):
    m.fill(c, 1, 1, 24, "#").fill(c, 9, 1, 4, ".").fill(c, 19, 1, 4, ".")
m.ledge(3, 4, 4, "1").ledge(31, 4, 4, "2")
m.ledge(9, 13, 4, "3").ledge(27, 13, 4, "4")
m.fill(1, 25, 38, 1, "%").fill(1, 23, 16, 2, "%").fill(23, 23, 16, 2, "%")
m.pad(4, 22, 4, "5").pad(31, 22, 4, "6").pad(18, 24, 4, "F")
m.fill(1, 18, 6, 1, "#").pad(2, 17, 4, "7")
m.fill(19, 1, 2, 22, "|", only_empty=True)
L.append(m)

# 4 ---------------------------------------------------------------
m = M("GIRDER CITY", 4, 1.0, 6)
m.fill(1, 25, 38, 1, "%")
m.gled(3, 5, 4, "1").gled(31, 5, 4, "2").gled(17, 9, 4, "3")
m.gled(8, 14, 4, "4").gled(26, 14, 4, "5")
m.gled(3, 20, 4, "F").gled(17, 19, 4, "6").gled(31, 20, 4, "7")
m.fill(11, 4, 4, 1, "=").fill(25, 4, 4, 1, "=")
L.append(m)

# 5 ---------------------------------------------------------------
m = M("ELEVATOR ACTION", 0, 1.0, 6)
m.fill(1, 25, 38, 1, "#")
m.ledge(2, 7, 4, "1").ledge(2, 16, 4, "2").ledge(33, 7, 4, "3").ledge(33, 16, 4, "4")
m.pad(2, 24, 4, "8").pad(18, 24, 4, "F").pad(33, 24, 4, "9")
m.fill(9, 1, 1, 24, "|", True).fill(30, 1, 1, 24, "|", True)
m.plat(12, 5, 12, 21, 4, 10.0, 0.0, "5")
m.plat(24, 21, 24, 5, 4, 10.0, 0.0, "6")
L.append(m)

# 6 ---------------------------------------------------------------
m = M("SHUTTLE RUN", 3, 1.0, 6)
m.fill(1, 25, 38, 1, "%")
m.ledge(2, 6, 4, "1", "%").ledge(33, 6, 4, "2", "%").ledge(2, 15, 4, "3", "%").ledge(33, 15, 4, "4", "%")
m.pad(2, 24, 4, "F").pad(33, 24, 4, "7")
m.fill(10, 1, 3, 3, "%").fill(27, 1, 3, 3, "%").fill(11, 4, 1, 1, "%").fill(28, 4, 1, 1, "%")
m.fill(18, 21, 4, 4, "%")
m.plat(8, 11, 28, 11, 4, 9.0, 0.0, "5")
m.plat(28, 19, 8, 19, 4, 9.5, 0.0, "6")
L.append(m)

# 7 ---------------------------------------------------------------
m = M("SWITCHBACK", 2, 1.0, 6)
m.fill(1, 13, 38, 1, "#").fill(6, 13, 4, 1, "D").fill(30, 13, 4, 1, "E")
m.ledge(3, 5, 4, "1").ledge(32, 5, 4, "2").pad(14, 12, 4, "F").fill(21, 12, 1, 1, "S")
m.fill(1, 25, 38, 1, "#")
m.pad(3, 24, 4, "3").pad(32, 24, 4, "4").ledge(17, 18, 4, "5").fill(12, 24, 1, 1, "S")
L.append(m)

# 8 ---------------------------------------------------------------
m = M("LASER GRID", 4, 1.0, 6)
m.fill(1, 25, 38, 1, "#")
m.fill(11, 13, 3, 2, "#").fill(26, 13, 3, 2, "#")
m.fill(12, 1, 1, 12, "L").fill(27, 1, 1, 12, "l")
m.fill(12, 15, 1, 10, "l").fill(27, 15, 1, 10, "L")
m.ledge(3, 6, 4, "1").ledge(33, 6, 4, "2").ledge(18, 9, 4, "3")
m.pad(3, 24, 4, "4").pad(33, 24, 4, "5").pad(17, 24, 6, "F")
m.ledge(4, 17, 4, "6")
L.append(m)

# 9 ---------------------------------------------------------------
m = M("UPDRAFT", 1, 1.1, 6)
m.fill(1, 20, 12, 5, "%").fill(27, 20, 12, 5, "%").fill(1, 25, 38, 1, "%")
m.pad(3, 19, 4, "1").pad(33, 19, 4, "2")
m.ledge(3, 9, 4, "3", "%").ledge(33, 9, 4, "4", "%")
m.pad(18, 24, 4, "F")
m.fill(13, 11, 3, 14, "^").fill(24, 11, 3, 14, "^")
m.fill(8, 4, 8, 3, ">").fill(24, 4, 8, 3, "<")
m.ledge(18, 14, 4, "5", "%")
L.append(m)

# 10 --------------------------------------------------------------
m = M("MOON BASE", 6, 0.55, 7)
m.fill(1, 22, 38, 4, "%")
m.fill(8, 20, 3, 2, "%").fill(29, 20, 3, 2, "%").fill(19, 21, 3, 1, "%")
m.pad(3, 21, 4, "1").pad(13, 21, 4, "2").pad(24, 21, 4, "3").pad(33, 21, 4, "F")
m.ledge(6, 10, 4, "4").ledge(30, 10, 4, "5").ledge(17, 15, 4, "6")
m.fill(14, 1, 1, 6, "#").fill(25, 1, 1, 6, "#")
L.append(m)

# 11 --------------------------------------------------------------
m = M("MINEFIELD", 5, 1.0, 6)
m.fill(1, 25, 38, 1, "%")
m.ledge(3, 5, 4, "1", "%").ledge(33, 5, 4, "2", "%").ledge(3, 15, 4, "3", "%").ledge(33, 15, 4, "4", "%")
m.pad(8, 24, 4, "5").pad(17, 24, 6, "F").pad(28, 24, 4, "6")
m.fill(18, 10, 4, 8, "#").pad(18, 9, 4, "7")
m.mine(7.5, 12, 32.5, 12, 8.0)
m.mine(13, 3, 13, 21, 6.0)
m.mine(26.5, 21, 26.5, 3, 6.0)
m.mine(20, 13, 0, 0, 9.0, 0.0, 7.0)
L.append(m)

# 12 --------------------------------------------------------------
m = M("WORMHOLE", 2, 1.0, 6, gate=4)
m.fill(1, 25, 38, 1, "#").fill(19, 1, 2, 24, "#")
m.ledge(3, 7, 4, "1").ledge(11, 13, 4, "2").pad(3, 24, 4, "F")
m.ledge(32, 7, 4, "3").ledge(24, 13, 4, "4").pad(32, 24, 4, "5")
m.fill(13, 20, 3, 3, "T").fill(24, 20, 3, 3, "t")
m.pad(26, 24, 4, "6")
L.append(m)

# 13 --------------------------------------------------------------
m = M("SPIKE PIT", 5, 1.0, 6)
m.fill(1, 25, 38, 1, "%").fill(1, 24, 38, 1, "X")
m.fill(3, 21, 6, 4, "%").pad(4, 20, 4, "1")
m.fill(16, 19, 8, 6, "%").pad(18, 18, 4, "F")
m.fill(31, 21, 6, 4, "%").pad(32, 20, 4, "2")
m.fill(1, 1, 16, 1, "X").fill(23, 1, 16, 1, "X")
m.ledge(6, 9, 4, "3", "%").fill(7, 13, 2, 1, "X")
m.ledge(30, 9, 4, "4", "%").fill(31, 13, 2, 1, "X")
m.ledge(12, 14, 4, "5", "%").ledge(25, 14, 4, "6", "%")
m.fill(1, 15, 1, 4, "X").fill(38, 15, 1, 4, "X")
L.append(m)

# 14 --------------------------------------------------------------
m = M("HEAVY METAL", 0, 1.5, 6)
m.fill(1, 25, 38, 1, "#")
m.pad(2, 24, 4, "1")
m.fill(8, 21, 6, 4, "#").pad(9, 20, 4, "2")
m.fill(16, 17, 8, 8, "#").pad(18, 16, 4, "F")
m.fill(26, 21, 6, 4, "#").pad(27, 20, 4, "3")
m.pad(34, 24, 4, "4")
m.ledge(4, 11, 4, "5").ledge(32, 11, 4, "6")
L.append(m)

# 15 --------------------------------------------------------------
m = M("LOCKDOWN", 4, 1.0, 7)
m.fill(1, 25, 38, 1, "#")
m.fill(13, 10, 14, 1, "#").fill(13, 17, 14, 1, "#").fill(13, 10, 1, 8, "#").fill(26, 10, 1, 8, "#")
m.fill(18, 10, 4, 1, "D").fill(18, 17, 4, 1, "E")
m.pad(14, 16, 4, "1").fill(24, 16, 1, 1, "S")
m.fill(8, 1, 1, 24, "L").fill(31, 1, 1, 24, "l")
m.ledge(2, 6, 4, "2").ledge(34, 6, 4, "3")
m.pad(2, 24, 4, "4").pad(34, 24, 4, "F").fill(19, 24, 1, 1, "S")
m.ledge(13, 5, 4, "5").ledge(23, 5, 4, "6")
L.append(m)

# 16 --------------------------------------------------------------
m = M("PISTONS", 0, 1.0, 6)
m.fill(1, 25, 38, 1, "#")
m.ledge(2, 8, 4, "1").ledge(34, 8, 4, "2")
m.pad(2, 24, 4, "3").pad(34, 24, 4, "4").pad(17, 24, 6, "F")
m.ledge(18, 12, 4, "5")
m.plat(9, 1, 9, 23, 3, 4.0, 0.0)
m.plat(28, 23, 28, 1, 3, 4.0, 0.0)
m.plat(13, 19, 24, 19, 3, 3.0, 0.0)
L.append(m)

# 17 --------------------------------------------------------------
m = M("CROSSWINDS", 3, 1.0, 7)
m.fill(1, 25, 38, 1, "%")
m.ledge(3, 4, 4, "1", "%").ledge(33, 4, 4, "2", "%").ledge(17, 12, 4, "3", "%")
m.pad(3, 24, 4, "4").pad(33, 24, 4, "5").pad(17, 24, 6, "F")
m.ledge(9, 19, 4, "6", "%").ledge(27, 19, 4, "7", "%")
m.fill(1, 8, 38, 3, ">", True).fill(1, 16, 38, 2, "<", True)
m.mine(5, 14, 34, 14, 7.0)
m.mine(34, 22, 5, 22, 9.0, 0.25)
L.append(m)

# 18 --------------------------------------------------------------
m = M("THE MAZE", 2, 1.0, 7)
m.fill(1, 25, 38, 1, "#")
m.fill(1, 8, 30, 1, "#").fill(10, 8, 4, 1, "D")
m.fill(9, 16, 30, 1, "#").fill(26, 16, 4, 1, "E")
m.fill(20, 7, 1, 1, "S").fill(21, 15, 1, 1, "S")
m.pad(3, 7, 4, "2").pad(25, 7, 4, "1")
m.pad(12, 15, 4, "3").pad(31, 15, 4, "4")
m.pad(3, 24, 4, "5").pad(14, 24, 4, "F").pad(25, 24, 4, "6")
m.fill(20, 19, 1, 6, "#").fill(8, 1, 1, 3, "#").fill(31, 1, 1, 3, "#")
m.fill(34, 3, 3, 3, "T").fill(34, 21, 3, 3, "t")
L.append(m)

# 19 --------------------------------------------------------------
m = M("ORBITAL", 6, 0.7, 7)
m.fill(1, 25, 38, 1, "%")
m.ledge(3, 5, 4, "1").ledge(33, 5, 4, "2")
m.pad(3, 24, 4, "3").pad(33, 24, 4, "F")
m.fill(17, 11, 6, 3, "#").fill(18, 14, 4, 1, "#").pad(18, 10, 4, "4")
m.plat(8, 21, 20, 21, 4, 9.0, 0.0, "5")
m.plat(28, 8, 28, 20, 4, 9.0, 0.0, "6")
m.mine(20, 12.5, 0, 0, 10.0, 0.0, 8.0)
m.mine(20, 12.5, 0, 0, 14.0, 0.5, 11.0)
L.append(m)

# 20 --------------------------------------------------------------
m = M("GRAND CENTRAL", 4, 1.0, 8)
m.fill(1, 25, 38, 1, "#")
m.ledge(3, 5, 4, "1").ledge(33, 5, 4, "2").ledge(3, 13, 4, "3").ledge(33, 13, 4, "4")
m.fill(1, 18, 9, 1, "#").fill(9, 18, 1, 7, "#").fill(9, 20, 1, 4, "D")
m.fill(30, 18, 9, 1, "#").fill(30, 18, 1, 7, "#").fill(30, 20, 1, 4, "E")
m.pad(3, 24, 4, "5").pad(33, 24, 4, "6").pad(17, 24, 6, "F")
m.fill(12, 24, 1, 1, "S").fill(27, 24, 1, 1, "S")
m.fill(13, 1, 1, 9, "L").fill(26, 1, 1, 9, "l")
m.fill(14, 15, 2, 9, "^")
m.plat(18, 7, 18, 19, 4, 9.0, 0.0, "7")
m.mine(10.5, 11, 29.5, 11, 6.0)
L.append(m)


# ---------------------------------------------------------------- validate
def pads_of(rows):
    out = []
    for y, r in enumerate(rows):
        x = 0
        while x < W:
            c = r[x]
            if c in "123456789F":
                x0 = x
                while x < W and r[x] == c: x += 1
                out.append((c, x0, y, x - x0))
            else:
                x += 1
    return out

errors = 0
for i, m in enumerate(L):
    rows = m.rows()
    assert len(rows) == H and all(len(r) == W for r in rows)
    pads = pads_of(rows)
    names = [p[0] for p in pads] + [p[7] for p in m.plats if p[7]]
    num = [n for n in names if n != "F"]
    if len(set(names)) != len(names):
        print(f"L{i+1}: duplicate pad ids {names}"); errors += 1
    if len(num) < 3:
        print(f"L{i+1}: needs >=3 numbered pads"); errors += 1
    slots = sum(max(1, (w * 8 - 4) // 20) for _, _, _, w in pads)
    if slots < 5:
        print(f"L{i+1}: only {slots} spawn slots"); errors += 1
    for c, x, y, w in pads:
        if w < 4:
            print(f"L{i+1}: pad {c} too narrow"); errors += 1
        for yy in range(y - 3, y):
            for xx in range(x, x + w):
                if rows[yy][xx] not in CLEAR:
                    print(f"L{i+1}: pad {c} blocked at {xx},{yy} by '{rows[yy][xx]}'"); errors += 1
    if "--show" in sys.argv:
        print(f"--- L{i+1} {m.name}")
        for r in rows: print(r)

if errors:
    print(errors, "errors"); sys.exit(1)

# ---------------------------------------------------------------- emit
out = os.path.join(os.path.dirname(__file__), "..", "src", "levels.inc")
with open(out, "w") as f:
    f.write("// generated by tools/levels_src.py - do not edit by hand\n")
    f.write("static void buildLevels(std::vector<LevelDef>& L) {\n")
    for m in L:
        f.write("    { LevelDef d; d.name = \"%s\"; d.theme = %d; d.gravity = %.2ff; d.quota = %d;\n" % (m.name, m.theme, m.g, m.quota))
        for r in m.rows():
            f.write('      d.map.push_back("%s");\n' % r)
        for p in m.plats:
            x0, y0, x1, y1, w, per, ph, pad, sw = p
            padc = "'%s'" % pad if pad else "0"
            F = lambda v: "%.3ff" % v
            f.write("      d.plats.push_back({%s,%s,%s,%s,%d,%s,%s,%s,%d});\n" % (F(x0), F(y0), F(x1), F(y1), w, F(per), F(ph), padc, sw))
        for mn in m.mines:
            f.write("      d.mines.push_back({%s});\n" % ",".join("%.3ff" % v for v in mn))
        f.write("      L.push_back(d); }\n")
    f.write("}\n")
print(f"OK: {len(L)} levels written to src/levels.inc")
