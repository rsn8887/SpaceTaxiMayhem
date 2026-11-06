#!/usr/bin/env python3
"""Level source for Space Taxi Mayhem.  Run to validate and emit src/levels.inc

Map legend (40x27 tiles of 8x8 px):
  .  empty            #  metal wall          %  rock wall        =  girder
  1-9 landing pad     F  fuel pad            G  exit gate (top row)
  D  door, closed while switch is OFF        E  door, closed while switch is ON
  S  floor switch (land on it to toggle)     L / l  laser beam phase A / B
  ^ < > v  wind      T / t  teleporter pair  X  spikes
  |  -  background pipes (decoration, not solid)
 levels 21-40 add:
  O  foreground (drawn over taxis)  n  thrust-dead field   c  cloaking field
  ~  water (deadly)                 Z  gun turret          R  radar dish (2x2 block, jams controls)
  Y / y  slow automatic door (phase 0 / 1/2): vertical runs slide up, horizontal runs slide left
  Q  switch of channel 1 (doors J closed while OFF, j closed while ON)
  U  switch of channel 2 (doors K closed while OFF, k closed while ON)
 levels 41-45 add:
  M  magnet field (gravity pulls up)   m  magnet (solid block, horseshoe graphic)
  r  controls-reversed field
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
        s.holes, s.air, s.weather, s.hint = [], [], 0, ""
        s.doorPeriod = s.gunPeriod = s.wind = None
        s.meteors, s.dark, s.sign = 0, False, None

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

    # moving wall / elevator block w x h tiles (no pad)
    def wall(s, x0, y0, x1, y1, w, h, period, phase=0.0, sw=0):
        s.plats.append((x0, y0, x1, y1, w, period, phase, "", sw, h)); return s

    # black hole: centre in tiles, pull px/s^2, deadly core radius px
    def hole(s, x, y, pull=60.0, core=7.0):
        s.holes.append((x, y, pull, core)); return s

    def spawn_air(s, x, y):
        s.air.append((x, y)); return s

    def put(s, x, y, c):
        s.t[y][x] = c; return s

    def water(s, y0):
        return s.fill(1, y0, W - 2, H - 1 - y0, "~")

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
m.fill(14, 6, 4, 19, "%").fill(22, 6, 4, 19, "%")
m.pad(14, 5, 4, "1").pad(22, 5, 4, "2")
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
m.plat(7, 11, 27, 11, 6, 9.0, 0.0, "5")      # 6 wide: easier to land on, same centre line as before
m.plat(27, 19, 7, 19, 6, 9.5, 0.0, "6")
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

# =================================================================
# Levels 21-40: four new graphics sets (7 oil rig / ocean, 8 jungle,
# 9 crystal cave, 10 the void) and new mechanics.
# =================================================================
OCEAN, JUNGLE, CAVE, VOID = 7, 8, 9, 10
VOLCANO, CIRCUIT = 11, 12

# 21 --------------------------------------------------------------
m = M("OIL RIG", OCEAN, 1.0, 5)
m.weather = 1; m.hint = "DON'T TOUCH THE WATER!"
m.water(23)
m.gled(3, 8, 5, "1").gled(32, 8, 5, "2").gled(16, 12, 6, "F")
m.gled(7, 17, 5, "3").gled(28, 17, 5, "4").gled(17, 20, 6, "5")
m.fill(10, 4, 9, 1, "=").fill(14, 5, 1, 18, "|", only_empty=True)   # crane boom on a leg down to the water
m.fill(1, 21, 3, 2, "%").fill(36, 21, 3, 2, "%")          # concrete pylons
L.append(m)

# 22 --------------------------------------------------------------
m = M("EVENT HORIZON", VOID, 1.0, 5)
m.hint = "BEWARE THE BLACK HOLE!"
m.hole(20, 13.5, 70, 7)
m.fill(1, 25, 38, 1, "%")
m.ledge(3, 6, 5, "1", "%").ledge(32, 6, 5, "2", "%").ledge(3, 19, 5, "3", "%").ledge(32, 19, 5, "4", "%")
m.pad(17, 24, 6, "F")
m.fill(10, 11, 3, 2, "%").fill(11, 13, 1, 1, "%").fill(27, 15, 3, 2, "%").fill(28, 14, 1, 1, "%")
m.fill(12, 3, 2, 1, "%").fill(26, 3, 2, 1, "%")
L.append(m)

# 23 --------------------------------------------------------------
m = M("CANOPY", JUNGLE, 1.0, 5)
m.weather = 3; m.hint = "TAXIS HIDE IN THE LEAVES"
m.fill(1, 24, 38, 2, "%")
m.ledge(3, 5, 5, "1", "%").ledge(32, 5, 5, "2", "%")
m.fill(11, 10, 2, 14, "%").fill(27, 10, 2, 14, "%")        # tree trunks
m.ledge(4, 18, 5, "3", "%").ledge(31, 18, 5, "4", "%").ledge(17, 15, 6, "5", "%")
m.pad(17, 23, 6, "F")
m.fill(1, 8, 38, 4, "O", only_empty=True)                    # the canopy...
for x, y, w, h in ((4, 9, 4, 2), (15, 8, 3, 3), (22, 10, 3, 2), (32, 9, 4, 2)):
    m.fill(x, y, w, h, ".")                                   # ...with a few gaps
m.fill(13, 6, 4, 3, "O", only_empty=True).fill(23, 6, 4, 3, "O", only_empty=True)
m.fill(1, 20, 2, 4, "O", only_empty=True).fill(37, 20, 2, 4, "O", only_empty=True)
L.append(m)

# 24 --------------------------------------------------------------
m = M("DEAD AIR", CAVE, 1.0, 5, gate=3)
m.hint = "ENGINES DIE IN THE BLUE FIELDS"
m.fill(1, 23, 38, 3, "%")
m.fill(8, 1, 3, 2, "%").fill(9, 3, 1, 1, "%").fill(29, 1, 3, 2, "%").fill(30, 3, 1, 1, "%")
m.ledge(3, 7, 5, "1", "%").ledge(32, 7, 5, "2", "%")
m.pad(3, 22, 5, "3").pad(32, 22, 5, "4").pad(17, 22, 6, "F")
m.fill(15, 5, 10, 1, "%").pad(17, 4, 6, "5")
m.fill(17, 8, 6, 11, "n")                                   # dead column in the middle
m.fill(1, 13, 11, 3, "n", only_empty=True).fill(28, 13, 11, 3, "n", only_empty=True)
L.append(m)

# 25 --------------------------------------------------------------
m = M("FOG BANK", OCEAN, 1.0, 5)
m.weather = 1; m.hint = "TAXIS VANISH IN THE FOG"
m.water(23)
m.fill(9, 3, 22, 6, "c")                                    # fog bank in the sky
m.fill(1, 17, 38, 6, "c")                                   # thick fog bank above the water (the rig legs vanish into it)
m.gled(2, 7, 5, "1").gled(33, 7, 5, "2").gled(9, 13, 5, "3").gled(26, 13, 5, "4").gled(17, 14, 6, "F")
m.gled(7, 21, 5, "6").gled(28, 21, 5, "5")                  # low pads just above the water, under pads 3 and 4,
m.fill(6, 19, 7, 2, ".").fill(27, 19, 7, 2, ".")            # with a small clear pocket so landed taxis stay visible
for x in (6, 9, 30, 33):                                     # legs of pads 1, 3, 4, 2 show again in the clear pockets
    m.fill(x, 19, 1, 2, "|")
L.append(m)

# 26 --------------------------------------------------------------
m = M("TEMPLE GATES", JUNGLE, 1.0, 5)
m.doorPeriod = 9.0; m.hint = "THE STONE DOORS MOVE SLOWLY"
m.fill(1, 24, 38, 2, "#")
m.fill(1, 12, 38, 1, "#").fill(8, 12, 4, 1, "Y").fill(28, 12, 4, 1, "y")
m.ledge(3, 7, 5, "1").ledge(32, 7, 5, "2").pad(17, 11, 6, "5")
m.fill(13, 13, 1, 11, "#").fill(13, 18, 1, 6, "y")
m.fill(26, 13, 1, 11, "#").fill(26, 18, 1, 6, "Y")
m.pad(3, 23, 5, "3").pad(17, 23, 6, "F").pad(32, 23, 5, "4")
m.fill(15, 3, 1, 4, "#").fill(24, 3, 1, 4, "#")             # pillars by the gate
L.append(m)

# 27 --------------------------------------------------------------
m = M("MINE SHAFT", CAVE, 1.0, 5, gate=4)
m.hint = "MIND THE MOVING WALLS"
m.fill(1, 24, 38, 2, "%")
m.fill(12, 4, 1, 19, "%").fill(12, 9, 1, 3, ".").fill(12, 16, 1, 3, ".")
m.fill(27, 4, 1, 19, "%").fill(27, 9, 1, 3, ".").fill(27, 16, 1, 3, ".")
m.ledge(3, 8, 5, "1", "%").ledge(32, 8, 5, "2", "%")
m.pad(3, 23, 5, "3").pad(32, 23, 5, "4").pad(17, 23, 6, "F")
m.fill(15, 6, 10, 1, "%").pad(17, 5, 6, "5")
m.fill(13, 7, 1, 1, "|").fill(26, 7, 1, 1, "|")
m.wall(13, 9, 21, 9, 6, 3, 6.0, 0.0)                         # pistons sweeping across the shaft
m.wall(21, 16, 13, 16, 6, 3, 7.0, 0.0)
m.wall(5, 13, 5, 19, 2, 2, 5.0, 0.25)                         # crushers in the side galleries
m.wall(33, 19, 33, 13, 2, 2, 5.0, 0.25)
L.append(m)

# 28 --------------------------------------------------------------
m = M("JAMMER STATION", VOID, 1.0, 5)
m.hint = "RADAR DISHES JAM YOUR CONTROLS"
m.fill(1, 24, 38, 2, "#")
m.fill(17, 15, 6, 9, "#")
m.fill(19, 13, 2, 2, "R")
m.ledge(3, 6, 5, "1").ledge(32, 6, 5, "2").ledge(8, 14, 5, "5")
m.fill(29, 17, 4, 1, "#").fill(30, 15, 2, 2, "R")
m.pad(2, 23, 5, "3").pad(33, 23, 5, "4").pad(24, 23, 5, "F")
L.append(m)

# 29 --------------------------------------------------------------
m = M("GUNBOATS", OCEAN, 1.0, 5)
m.gunPeriod = 3.0; m.hint = "TURRETS OPEN FIRE!"
m.water(23)
m.gled(2, 7, 5, "1").gled(33, 7, 5, "2").gled(10, 14, 5, "3").gled(25, 14, 5, "4").gled(17, 19, 6, "F")
m.fill(19, 3, 2, 8, "%").put(19, 6, "Z").put(20, 9, "Z")          # gun tower in the middle
m.fill(6, 18, 2, 5, "%").put(6, 18, "Z").fill(32, 18, 2, 5, "%").put(33, 18, "Z")
L.append(m)

# 30 --------------------------------------------------------------
m = M("MONSOON", JUNGLE, 1.0, 5)
m.weather = 1; m.hint = "FLOODED RIVER AND FALLING LOGS"
m.fill(1, 20, 9, 6, "%").fill(30, 20, 9, 6, "%")
m.fill(10, 22, 20, 4, "~")
m.ledge(2, 6, 5, "1", "%").ledge(33, 6, 5, "2", "%").ledge(17, 9, 6, "F", "%")
m.pad(3, 19, 5, "3").pad(32, 19, 5, "4")
m.plat(11, 18, 25, 18, 4, 9.0, 0.0, "5")                      # river ferry
m.wall(12, 2, 12, 12, 2, 4, 6.0, 0.0)                         # logs sliding up and down
m.wall(26, 12, 26, 2, 2, 4, 6.0, 0.0)
L.append(m)

# 31 --------------------------------------------------------------
m = M("DARK HOLLOW", CAVE, 1.0, 5)
m.hint = "INVISIBLE IN THE DARK MIST"
m.fill(1, 1, 38, 2, "%").fill(18, 1, 4, 2, ".")
m.fill(1, 23, 38, 3, "%")
m.fill(11, 12, 18, 2, "%").fill(14, 14, 12, 1, "X")
m.ledge(3, 7, 4, "1", "%").ledge(33, 7, 4, "2", "%")
m.pad(12, 11, 5, "3").pad(23, 11, 5, "4").pad(17, 22, 6, "F")
m.fill(8, 3, 24, 5, "c", only_empty=True)
m.fill(1, 15, 38, 4, "c", only_empty=True)
L.append(m)

# 32 --------------------------------------------------------------
m = M("TWIN SINGULARITY", VOID, 1.0, 4)
m.hint = "TWO BLACK HOLES!"
m.hole(11, 13, 55, 6).hole(29, 13, 55, 6)
m.fill(1, 25, 38, 1, "%")
m.ledge(3, 5, 5, "1", "%").ledge(32, 5, 5, "2", "%")
m.fill(18, 13, 4, 2, "%").pad(18, 12, 4, "3")
m.pad(18, 24, 4, "F")
m.spawn_air(20, 5).spawn_air(6, 21).spawn_air(34, 21)
L.append(m)

# 33 --------------------------------------------------------------
m = M("ARCTIC SEA", OCEAN, 1.0, 5)
m.weather = 2; m.doorPeriod = 8.0; m.hint = "THE SHUTTERS OPEN AND CLOSE"
m.water(23)
m.fill(1, 12, 38, 1, "#").fill(6, 12, 4, 1, "Y").fill(30, 12, 4, 1, "y")
m.ledge(3, 6, 5, "1").ledge(32, 6, 5, "2").pad(16, 11, 4, "5").pad(22, 11, 4, "F")
m.fill(12, 13, 1, 10, "#").fill(12, 16, 1, 4, "y")
m.fill(27, 13, 1, 10, "#").fill(27, 16, 1, 4, "Y")
m.fill(2, 23, 7, 3, "%").pad(3, 22, 5, "3")
m.fill(31, 23, 7, 3, "%").pad(32, 22, 5, "4")
m.gled(17, 18, 6, "6")
L.append(m)

# 34 --------------------------------------------------------------
m = M("SNIPER JUNGLE", JUNGLE, 1.0, 5)
m.weather = 3; m.gunPeriod = 2.8; m.hint = "SNIPERS IN THE TREES!"
m.fill(1, 23, 38, 3, "%")
m.fill(10, 5, 2, 18, "%").fill(28, 5, 2, 18, "%")
m.put(11, 9, "Z").put(11, 16, "Z").put(28, 11, "Z").put(28, 18, "Z")       # all facing the middle
m.ledge(2, 10, 4, "1", "%").ledge(34, 10, 4, "2", "%").ledge(17, 12, 6, "5", "%")
m.pad(2, 22, 6, "3").pad(32, 22, 6, "4").pad(17, 22, 6, "F")
m.fill(4, 2, 12, 4, "O", only_empty=True).fill(24, 2, 12, 4, "O", only_empty=True)
m.fill(12, 19, 4, 3, "O", only_empty=True).fill(24, 19, 4, 3, "O", only_empty=True)
m.fill(12, 8, 4, 2, "O", only_empty=True).fill(24, 14, 4, 2, "O", only_empty=True)
L.append(m)

# 35 --------------------------------------------------------------
m = M("CRYSTAL LOCKS", CAVE, 1.0, 6)
m.hint = "THREE SWITCHES, THREE COLOURS"
# top tier rows 1-8, middle tier rows 10-18, bottom tier rows 20-24, one-row floor
m.fill(1, 9, 38, 1, "#").fill(1, 19, 38, 1, "#").fill(1, 25, 38, 1, "%")      # metal decks: flat for the switches
m.fill(13, 10, 1, 9, "%").fill(26, 10, 1, 9, "%").fill(13, 20, 1, 5, "%").fill(26, 20, 1, 5, "%")
m.fill(4, 9, 4, 1, "D").fill(32, 9, 4, 1, "E").fill(17, 9, 6, 1, ".")
m.fill(13, 13, 1, 4, "J").fill(26, 13, 1, 4, "j")
m.fill(4, 19, 4, 1, "k").fill(20, 19, 4, 1, "K").fill(32, 19, 4, 1, "E")
m.fill(13, 21, 1, 4, "D").fill(26, 21, 1, 4, "J")
m.fill(2, 5, 7, 1, "%").pad(3, 4, 5, "1").fill(31, 5, 7, 1, "%").pad(32, 4, 5, "2").put(28, 8, "S")   # thin rock shelves
m.fill(2, 15, 7, 1, "%").pad(3, 14, 5, "3").fill(31, 15, 7, 1, "%").pad(32, 14, 5, "4")   # raised: the doors below stay clear
m.put(10, 18, "Q").put(28, 18, "U").pad(14, 18, 5, "5")
m.pad(3, 24, 5, "6").pad(17, 24, 6, "F").pad(32, 24, 5, "7")
L.append(m)

# 36 --------------------------------------------------------------
m = M("DERELICT", VOID, 1.0, 5)
m.gunPeriod = 3.2; m.hint = "INSIDE THE HULL NOBODY SEES YOU"
m.fill(1, 23, 38, 3, "#")
m.fill(8, 12, 24, 1, "#").fill(8, 19, 24, 1, "#")
m.pad(10, 11, 5, "1").pad(25, 11, 5, "2").pad(17, 18, 6, "F")
m.pad(2, 22, 5, "3").pad(33, 22, 5, "4").ledge(2, 4, 4, "5")
m.fill(7, 5, 26, 14, "O", only_empty=True)
for x, y, w, h in ((9, 8, 7, 3), (24, 8, 7, 3), (16, 15, 8, 3), (13, 6, 2, 1), (27, 6, 2, 1), (10, 16, 2, 2), (29, 16, 2, 2)):
    m.fill(x, y, w, h, ".")
m.put(12, 23, "Z").put(27, 23, "Z").put(0, 8, "Z").put(39, 15, "Z")
L.append(m)

# 37 --------------------------------------------------------------
m = M("ECHO CHAMBER", CAVE, 1.0, 5)
m.hint = "JAMMERS AND DEAD ZONES"
m.fill(1, 23, 38, 3, "%")
m.fill(6, 1, 4, 2, "%").fill(30, 1, 4, 2, "%")
m.ledge(2, 6, 5, "1", "%").ledge(33, 6, 5, "2", "%").ledge(10, 15, 4, "5", "%")
m.pad(2, 22, 5, "3").pad(33, 22, 5, "4").pad(17, 22, 6, "F")
m.fill(10, 21, 2, 2, "R").fill(28, 21, 2, 2, "R")
m.fill(17, 5, 6, 13, "n").fill(3, 11, 7, 2, "n", only_empty=True).fill(30, 13, 7, 2, "n", only_empty=True)
L.append(m)

# 38 --------------------------------------------------------------
m = M("HURRICANE", OCEAN, 1.0, 5)
m.weather = 1; m.hint = "STORM WINDS AND JAMMERS"
m.water(23)
m.gled(2, 7, 5, "1").gled(33, 7, 5, "2").gled(8, 17, 5, "3").gled(27, 17, 5, "4").gled(17, 20, 6, "F")
m.fill(1, 11, 38, 3, "<", only_empty=True)
m.fill(17, 9, 6, 1, "=").fill(19, 7, 2, 2, "R").fill(18, 10, 1, 1, "|").fill(21, 10, 1, 1, "|")
m.wall(4, 3, 33, 3, 3, 2, 10.0, 0.0)                         # crane load swinging across the top
L.append(m)

# 39 --------------------------------------------------------------
m = M("LOST TEMPLE", JUNGLE, 1.0, 6)
m.weather = 3; m.doorPeriod = 10.0; m.hint = "SWITCHES, GATES AND OVERGROWTH"
m.fill(1, 23, 38, 3, "#")
m.fill(1, 13, 38, 1, "#").fill(5, 13, 4, 1, "Y").fill(30, 13, 4, 1, "J").fill(11, 14, 6, 1, "X")
m.ledge(3, 7, 5, "1").ledge(32, 7, 5, "2").pad(14, 12, 5, "5").put(23, 12, "Q")
m.fill(19, 14, 2, 9, "#").fill(19, 18, 2, 5, "y")
m.pad(3, 22, 5, "3").pad(11, 22, 5, "F").pad(32, 22, 5, "4").pad(24, 22, 4, "6")
m.fill(1, 1, 7, 3, "O", only_empty=True).fill(32, 1, 7, 3, "O", only_empty=True)
m.fill(28, 14, 8, 5, "O", only_empty=True).fill(6, 15, 3, 3, "O", only_empty=True)   # overgrowth under the yellow door
m.fill(10, 1, 1, 12, "#").fill(10, 4, 1, 5, "j")            # wall right of pad 1: purple door, closed when the switch opens yellow
L.append(m)

# 40 --------------------------------------------------------------
m = M("MAYHEM", VOID, 1.0, 6)
m.gunPeriod = 3.0; m.doorPeriod = 9.0; m.hint = "EVERYTHING AT ONCE!"
m.hole(20, 16, 50, 6)
m.fill(1, 24, 38, 2, "#")
m.ledge(3, 5, 5, "1").ledge(32, 5, 5, "2").ledge(3, 14, 5, "3").ledge(32, 14, 5, "4")
m.fill(10, 16, 1, 8, "#").fill(10, 19, 1, 5, "Y").pad(3, 23, 6, "F")
m.fill(29, 16, 1, 8, "#").fill(29, 19, 1, 5, "y").pad(31, 23, 6, "5")
m.fill(11, 9, 18, 3, "c")
m.fill(27, 8, 4, 1, "#").fill(28, 6, 2, 2, "R")
m.put(0, 11, "Z").put(39, 11, "Z").put(0, 21, "Z").put(39, 21, "Z")
m.spawn_air(20, 4).spawn_air(8, 11).spawn_air(32, 11)
L.append(m)


# 41 --------------------------------------------------------------
# A stepped mountain from the top left down to the bottom right (after Air Taxi):
# a pad on every step, fuel at the bottom, and a steady wind blowing left over all of it.
m = M("VOLCANIC STAIRWAY", VOLCANO, 1.0, 6)
m.weather = 3; m.wind = 0.4; m.hint = "A STRONG WIND BLOWS TO THE LEFT"
steps = [(1, 5, 5, "1"), (7, 11, 9, "2"), (13, 17, 13, "3"), (19, 23, 17, "4"), (25, 29, 21, "5")]
for x0, x1, top, pad in steps:
    m.fill(x0, top, x1 - x0 + 1, H - 1 - top, "%")              # step and everything below it
    m.fill(x1 + 1, top + 2, 1, H - 3 - top, "%")                 # sloping edge down to the next step
    m.pad(x0, top - 1, x1 - x0 + 1, pad)
m.fill(31, 25, 8, 1, "%").pad(32, 24, 6, "F")                   # the bottom, with the fuel pad
m.fill(1, 1, 38, 24, "<", only_empty=True)                      # wind everywhere above the mountain
L.append(m)

# =================================================================
# Levels 42-45: volcano world (11) and circuit board world (12)
# =================================================================

# 42 --------------------------------------------------------------
# Two magnets hang over pads 1 and 2: inside their fields gravity pulls UP, into the spikes.
m = M("MAGNET LAB", CIRCUIT, 1.0, 5)
m.hint = "MAGNETS PULL YOU UP!"
m.fill(1, 24, 38, 2, "#")
for x0, pad, low in ((3, "1", "3"), (31, "2", "4")):
    m.fill(x0, 1, 6, 2, "m")                         # the magnet
    m.fill(x0, 3, 6, 1, "#")                         # its mounting plate
    m.fill(x0, 4, 6, 1, "X")                         # spikes pointing down at the taxis
    m.fill(x0, 5, 6, 7, "M")                         # the field: everything here falls upwards
    m.pad(x0 + 1, 13, 4, pad).fill(x0, 14, 6, 2, "#")   # the pad right under it (one free row above), on a flat block
    m.fill(x0, 16, 6, 1, "X")                        # spikes under the block, facing the lower field
    m.fill(x0, 17, 6, 5, "M", only_empty=True)       # a second field under the pad's support...
    m.pad(x0 + 1, 23, 4, low)                        # ...above the pad below (one free row above it)
m.ledge(17, 13, 6, "5")
m.pad(17, 23, 6, "F")
m.fill(13, 18, 3, 2, "#").fill(24, 18, 3, 2, "#")   # chips on the board
L.append(m)

# 43 --------------------------------------------------------------
# Meteors rain down diagonally; the rock tower's overhang shelters pad 3.
m = M("METEOR SHOWER", VOLCANO, 1.0, 5)
m.meteors = 1.4; m.hint = "TAKE COVER FROM THE METEORS!"
m.fill(1, 24, 38, 2, "%")
m.fill(2, 22, 7, 2, "%").pad(3, 21, 5, "1")           # pads near the bottom
m.fill(31, 22, 7, 2, "%").pad(32, 21, 5, "2")
m.pad(25, 23, 5, "F")                                 # fuel in the gap between the tower and pad 2
m.fill(22, 14, 3, 10, "%").fill(21, 13, 6, 1, "%").pad(21, 12, 6, "4")   # rising tower with a pad on top
m.fill(15, 17, 7, 2, "%")                             # overhang off the tower
m.pad(16, 23, 5, "3")                                 # ...sheltering the pad below it
L.append(m)

# 44 --------------------------------------------------------------
# Nearly the whole level reverses the controls; only small areas around the pads are normal.
m = M("INVERTER", CIRCUIT, 1.0, 5)
m.hint = "CONTROLS ARE REVERSED!"
m.fill(1, 24, 38, 2, "#")
m.ledge(2, 6, 5, "1").ledge(33, 6, 5, "2")
m.pad(2, 23, 5, "3").pad(33, 23, 5, "4").pad(17, 23, 6, "F")
m.fill(9, 15, 3, 3, "#").fill(28, 11, 3, 3, "#")   # chips in the way
m.fill(1, 1, 38, 23, "r", only_empty=True)
for x, y, w in ((2, 6, 5), (33, 6, 5), (2, 23, 5), (33, 23, 5), (17, 23, 6)):
    m.fill(x - 1, y - 4, w + 2, 4, ".")              # normal controls right around the pads
m.sign = (20, 12)
L.append(m)

# 45 --------------------------------------------------------------
# Pitch dark: only the taxis can be seen, and lightning now and then shows the rest.
m = M("BLACKOUT", VOLCANO, 1.0, 5)
m.dark = True; m.hint = "WAIT FOR THE LIGHTNING!"
m.fill(1, 24, 38, 2, "%")
m.ledge(2, 8, 8, "1", "%").ledge(30, 8, 8, "2", "%").ledge(2, 17, 8, "3", "%").ledge(30, 17, 8, "4", "%")
m.pad(16, 23, 8, "F")
m.fill(19, 9, 2, 9, "%").fill(14, 13, 3, 2, "%").fill(23, 13, 3, 2, "%")
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

# reachability for levels 21+: a taxi-sized box (3x2 tiles) must be able to fly from
# every pad to every other pad and to the exit gate (doors count as open, moving walls
# are ignored, black-hole cores, water, spikes, turrets and radar dishes block)
BLOCK = set("#%=123456789FGZRX~m")
def check_new(i, m, rows, pads):
    err = 0
    blocked = [[rows[y][x] in BLOCK for x in range(W)] for y in range(H)]
    for (hx, hy, pull, core) in m.holes:
        for y in range(H):
            for x in range(W):
                if ((x + 0.5 - hx) ** 2 + (y + 0.5 - hy) ** 2) ** 0.5 * 8 < core + 6:
                    blocked[y][x] = True
    def free(x, y):
        if x < 0 or x + 3 > W or y < 0 or y + 2 > H: return False
        return all(not blocked[yy][xx] for yy in range(y, y + 2) for xx in range(x, x + 3))
    targets = {}
    for c, x, y, w in pads:
        targets[c] = (x + (w - 3) // 2, y - 2)
    for p in m.plats:
        if p[7]: targets[p[7]] = None        # moving pads: not checked
    gx = rows[0].index("G")
    targets["gate"] = (gx, 1)
    tp = [(x, y) for y in range(H) for x in range(W) if rows[y][x] in "Tt"]
    start = targets["gate"]
    if not free(*start):
        print(f"L{i+1}: gate approach blocked"); return 1
    seen = {start}; q = [start]
    while q:
        x, y = q.pop()
        nb = [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
        for a, b in nb:
            if (a, b) not in seen and free(a, b):
                seen.add((a, b)); q.append((a, b))
    for name, pos in targets.items():
        if pos is None: continue
        if pos not in seen:
            print(f"L{i+1}: pad {name} not reachable from the exit gate"); err += 1
    for (ax, ay) in m.air:
        x, y = int(ax) - 1, int(ay) - 1
        if not free(x, y):
            print(f"L{i+1}: air spawn {ax},{ay} blocked"); err += 1
    sw = sum(r.count(c) for r in rows for c in "SQU")
    if any(c in "DEJjKk" for r in rows for c in r) and sw == 0:
        print(f"L{i+1}: switch doors but no switches"); err += 1
    return err

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
    slots = sum(max(1, (w * 8 - 4) // 20) for _, _, _, w in pads) + len(m.air)
    if slots < 5:
        print(f"L{i+1}: only {slots} spawn slots"); errors += 1
    for c, x, y, w in pads:
        if w < 4:
            print(f"L{i+1}: pad {c} too narrow"); errors += 1
        for yy in range(y - 3, y):
            for xx in range(x, x + w):
                if rows[yy][xx] not in CLEAR and not (i >= 20 and yy == y - 3 and rows[yy][xx] == "c") \
                        and not (i >= 40 and rows[yy][xx] in "Mr"):   # a taxi on the pad fits in 2 rows / fields above pads
                    print(f"L{i+1}: pad {c} blocked at {xx},{yy} by '{rows[yy][xx]}'"); errors += 1
    if i >= 20:
        errors += check_new(i, m, rows, pads)
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
        F = lambda v: "%.3ff" % v
        for p in m.plats:
            x0, y0, x1, y1, w, per, ph, pad, sw = p[:9]
            padc = "'%s'" % pad if pad else "0"
            h = ",%d" % p[9] if len(p) > 9 else ""
            f.write("      d.plats.push_back({%s,%s,%s,%s,%d,%s,%s,%s,%d%s});\n" % (F(x0), F(y0), F(x1), F(y1), w, F(per), F(ph), padc, sw, h))
        for mn in m.mines:
            f.write("      d.mines.push_back({%s});\n" % ",".join("%.3ff" % v for v in mn))
        for hl in m.holes:
            f.write("      d.holes.push_back({%s});\n" % ",".join(F(v) for v in hl))
        for a in m.air:
            f.write("      d.air.push_back({%s,%s});\n" % (F(a[0]), F(a[1])))
        if m.weather: f.write("      d.weather = %d;\n" % m.weather)
        if m.doorPeriod: f.write("      d.doorPeriod = %s;\n" % F(m.doorPeriod))
        if m.gunPeriod: f.write("      d.gunPeriod = %s;\n" % F(m.gunPeriod))
        if m.hint: f.write('      d.hint = "%s";\n' % m.hint)
        if m.wind: f.write("      d.wind = %s;\n" % F(m.wind))
        if m.meteors: f.write("      d.meteors = %s;\n" % F(m.meteors))
        if m.dark: f.write("      d.dark = true;\n")
        if m.sign: f.write("      d.sign = {%s,%s};\n" % (F(m.sign[0]), F(m.sign[1])))
        f.write("      L.push_back(d); }\n")
    f.write("}\n")
print(f"OK: {len(L)} levels written to src/levels.inc")
