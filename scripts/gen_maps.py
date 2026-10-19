"""Generatorul hartilor de joc (assets/maps/*.txt).

Hartile sunt construite din primitive (cladiri, camere, usi, ferestre, acoperiri) si
verificate automat: dimensiuni constante, toti inamicii / obiectivele accesibili din
zona de start (pe jos, prin usi). Ruleaza: python scripts/gen_maps.py

Legenda (src/sim/surface.hpp): '1' beton, '2' lemn, '3' metal, '4' rigips, '5' piatra,
'6' fereastra, 'h' zid scund, 'D' usa, 'X' usa incuiata; podele '.', 'w', 'r', 'L' (lampa),
'm', 's' (interior), ',' pamant, ';' asfalt, 'g' iarba (exterior); markeri S E p O c b k z P C K.
"""
import os
import sys
from collections import deque

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "maps")

WALLS = set("1234565")
SOLID = set("123456")  # pereti care blocheaza mersul
PASSABLE_FLOOR = set(".wrLms,;g")
MARKERS = set("SEpOcbkzyPCK")


class Grid:
    def __init__(self, w, h, floor=";"):
        self.w, self.h = w, h
        self.g = [[floor] * w for _ in range(h)]
        self.border()

    def border(self, c="1"):
        for x in range(self.w):
            self.g[0][x] = c
            self.g[self.h - 1][x] = c
        for y in range(self.h):
            self.g[y][0] = c
            self.g[y][self.w - 1] = c

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = c

    def get(self, x, y):
        return self.g[y][x]

    def fill(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def room(self, x0, y0, x1, y1, wall="1", floor="."):
        """Perimetru de perete + podea interioara (coordonate inclusive ale peretilor)."""
        self.fill(x0, y0, x1, y1, wall)
        self.fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, floor)

    def hwall(self, x0, x1, y, c="1"):
        for x in range(x0, x1 + 1):
            self.set(x, y, c)

    def vwall(self, x, y0, y1, c="1"):
        for y in range(y0, y1 + 1):
            self.set(x, y, c)

    def lamps(self, x0, y0, x1, y1, step=4):
        for y in range(y0 + 1, y1, step):
            for x in range(x0 + 1, x1, step):
                if self.get(x, y) in ".wrms":
                    self.set(x, y, "L")

    def text(self):
        return "\n".join("".join(r) for r in self.g) + "\n"


def check(grid, name):
    rows = grid.text().strip("\n").split("\n")
    assert all(len(r) == grid.w for r in rows), name + ": randuri inegale"
    starts = [(x, y) for y in range(grid.h) for x in range(grid.w) if grid.get(x, y) == "S"]
    assert starts, name + ": fara spawn S"

    def passable(x, y):
        c = grid.get(x, y)
        return c in PASSABLE_FLOOR or c in MARKERS or c in "DX"

    seen = set()
    q = deque([starts[0]])
    seen.add(starts[0])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < grid.w and 0 <= ny < grid.h and (nx, ny) not in seen and passable(nx, ny):
                seen.add((nx, ny))
                q.append((nx, ny))
    bad = [(c, x, y) for y in range(grid.h) for x in range(grid.w)
           for c in [grid.get(x, y)] if c in "EOSp" and (x, y) not in seen]
    assert not bad, name + ": inaccesibili " + str(bad[:6])
    counts = {k: sum(r.count(k) for r in rows) for k in "SEO"}
    print("%-12s %3dx%-3d  S=%d E=%d O=%d  celule accesibile=%d" % (name, grid.w, grid.h, counts["S"], counts["E"],
                                                                  counts["O"], len(seen)))


# ============================================================================
def urban():
    """Strazi in cruce, blocuri de apartamente cu holuri si camere, magazine cu vitrine.
    Distante medii (10-30 m), multe acoperiri (masini, ziduri scunde), trei obiective."""
    G = Grid(64, 44, ";")
    # trotuare (pamant batut) langa cladiri - raman exterior
    # Cartierul NV: bloc de apartamente cu hol central
    G.room(3, 3, 26, 16, "1", "w")
    G.hwall(4, 25, 9, "4")
    G.hwall(4, 25, 11, "4")
    G.fill(4, 10, 25, 10, ".")            # holul
    for x in (9, 15, 21):
        G.vwall(x, 4, 8, "4")
        G.vwall(x, 12, 15, "4")
    for x in (6, 12, 18, 23):
        G.set(x, 9, "D")
        G.set(x, 11, "D")
    G.set(3, 10, "D")                     # intrarea dinspre vest
    G.set(26, 10, "D")                    # intrarea dinspre strada verticala
    for x in (5, 7, 11, 13, 17, 19, 23, 25):
        G.set(x, 3, "6")
    for x in (6, 12, 18, 24):
        G.set(x, 16, "6")
    G.lamps(3, 3, 26, 16, 5)
    # Cartierul NE: magazin cu vitrina + depozit
    G.room(37, 3, 60, 16, "1", ".")
    G.vwall(49, 4, 15, "4")
    G.set(49, 7, "D")
    G.set(49, 13, "D")
    for x in range(39, 48, 2):
        G.set(x, 16, "6")                 # vitrina spre strada
    G.set(43, 16, "D")
    G.set(55, 16, "D")
    G.fill(51, 5, 52, 14, "3")            # rafturi metalice
    G.fill(56, 5, 57, 14, "3")
    G.lamps(37, 3, 49, 16, 4)
    # Cartierul SV: cladire de birouri cu doua aripi
    G.room(3, 27, 26, 40, "1", "r")
    G.hwall(4, 25, 33, "4")
    G.vwall(14, 28, 32, "4")
    G.vwall(14, 34, 39, "4")
    G.set(14, 30, "D")
    G.set(14, 37, "D")
    G.set(8, 33, "D")
    G.set(20, 33, "D")
    G.set(26, 30, "D")
    G.set(10, 27, "D")
    for y in (29, 31, 36, 38):
        G.set(3, y, "6")
    for x in (6, 18, 22):
        G.set(x, 27, "6")
    G.lamps(3, 27, 26, 40, 4)
    # Cartierul SE: parcare inchisa cu zid scund + garaj
    G.room(46, 30, 60, 40, "1", ".")
    G.set(46, 34, "D")
    G.set(52, 30, "D")
    G.lamps(46, 30, 60, 40, 4)
    for x in range(36, 45):
        G.set(x, 28, "h")
    for y in range(28, 41):
        G.set(36, y, "h")
    G.set(36, 33, ";")                     # trecere prin zid
    # masini parcate (metal) pe strazi: acoperire
    for (x, y) in ((8, 20), (19, 23), (44, 20), (54, 23), (30, 7), (33, 34)):
        G.fill(x, y, x + 3, y + 1, "3") if y in (20, 23) else G.fill(x, y, x + 1, y + 3, "3")
    for (x, y) in ((14, 18), (40, 25), (22, 25), (31, 13)):
        G.fill(x, y, x + 2, y, "h")
    # markeri
    G.set(2, 21, "S"); G.set(2, 22, "S"); G.set(2, 20, "S")
    for (x, y) in ((22, 6), (12, 14), (45, 12), (55, 9), (20, 37), (8, 30), (52, 36)):
        G.set(x, y, "E")
    for (x, y) in ((31, 21), (31, 5), (31, 38), (60, 21), (16, 10), (42, 34)):
        G.set(x, y, "p")
    G.set(18, 12, "O"); G.set(43, 6, "O"); G.set(8, 36, "O")
    G.set(5, 5, "c"); G.set(39, 9, "c"); G.set(58, 38, "b"); G.set(29, 26, "b"); G.set(24, 38, "k")
    G.set(25, 14, "C"); G.set(58, 4, "K"); G.set(47, 39, "P")
    return G


def industrial():
    """Hale mari cu rafturi, curte de containere, zone deschise: distante lungi (30-60 m)."""
    G = Grid(78, 50, ",")
    # Hala A (nord-vest): rafturi lungi -> culoare lungi de tragere
    G.room(3, 3, 40, 20, "1", ".")
    for y in (7, 11, 15):
        G.fill(8, y, 34, y, "3")
    G.set(3, 9, "D"); G.set(3, 17, "D"); G.set(40, 5, "D"); G.set(22, 20, "D"); G.set(30, 20, "D")
    for x in range(6, 38, 4):
        G.set(x, 3, "6")
    G.lamps(3, 3, 40, 20, 5)
    # Hala B (nord-est): rampa de incarcare cu birou
    G.room(48, 3, 75, 22, "1", ".")
    G.room(66, 4, 75, 11, "1", "r")       # birou
    G.set(66, 8, "D")
    G.set(70, 11, "6"); G.set(72, 11, "6")
    for x in (53, 57, 61):
        G.fill(x, 8, x + 1, 17, "3")      # stive de paleti / rafturi
    G.set(48, 12, "D"); G.set(56, 22, "D"); G.set(64, 22, "D")
    G.lamps(48, 3, 75, 22, 5)
    # Curtea de containere (sud): culoare lungi intre stive
    for y in (27, 33, 39):
        for x0 in (6, 20, 34, 48):
            G.fill(x0, y, x0 + 9, y + 2, "3")
    G.fill(62, 28, 64, 44, "3")
    # Statia de pompare (sud-est), cu ferestre
    G.room(68, 30, 76, 46, "1", "m")
    G.set(68, 38, "D")
    G.set(72, 30, "6")
    G.lamps(68, 30, 76, 46, 4)
    # ziduri scunde / butoaie: acoperiri in spatiile deschise
    for (x, y) in ((44, 24), (12, 24), (28, 45), (52, 45)):
        G.fill(x, y, x + 3, y, "h")
    G.fill(44, 3, 44, 18, "h")
    # markeri
    G.set(2, 46, "S"); G.set(2, 45, "S"); G.set(3, 46, "S")
    for (x, y) in ((36, 13), (20, 5), (60, 19), (72, 7), (57, 37), (72, 42), (30, 30)):
        G.set(x, y, "E")
    for (x, y) in ((45, 26), (25, 36), (60, 46), (10, 22), (70, 25)):
        G.set(x, y, "p")
    G.set(22, 9, "O"); G.set(70, 8, "O"); G.set(41, 36, "O")
    G.set(5, 5, "c"); G.set(39, 18, "b"); G.set(50, 20, "b"); G.set(46, 30, "b"); G.set(18, 44, "c"); G.set(67, 26, "k")
    G.set(74, 5, "C"); G.set(70, 45, "P"); G.set(5, 19, "K")
    return G


def residential():
    """Case cu camere mici, gradini cu garduri (ziduri scunde), multe colturi: distante scurte."""
    G = Grid(54, 42, "g")
    G.fill(1, 19, 52, 22, ";")             # strada
    G.fill(25, 1, 28, 40, ",")             # alee
    def house(x0, y0, door_side, floor="w"):
        x1, y1 = x0 + 17, y0 + 12
        G.room(x0, y0, x1, y1, "2", floor)
        mx, my = x0 + 8, y0 + 6
        G.vwall(mx, y0 + 1, y1 - 1, "4")
        G.hwall(x0 + 1, mx - 1, my, "4")
        G.hwall(mx + 1, x1 - 1, my - 2, "4")
        G.set(mx, y0 + 3, "D"); G.set(mx, y1 - 3, "D")
        G.set(x0 + 4, my, "D"); G.set(x1 - 4, my - 2, "D")
        G.set(x0 + 3, y0, "6"); G.set(x1 - 3, y0, "6"); G.set(x0 + 3, y1, "6"); G.set(x1 - 3, y1, "6")
        G.set(x0, y0 + 4, "6"); G.set(x1, y1 - 4, "6")
        if door_side == "s":
            G.set(x0 + 12, y1, "D")
        else:
            G.set(x0 + 12, y0, "D")
        G.lamps(x0, y0, x1, y1, 4)
        return (x0, y0, x1, y1)
    houses = [house(3, 3, "s"), house(32, 3, "s", "r"), house(3, 26, "n"), house(32, 26, "n", "r")]
    # garduri de gradina (ziduri scunde) cu porti
    for (x0, y0, x1, y1) in houses:
        top = y0 - 1 if y0 > 3 else None
        gy = y1 + 3 if y0 < 19 else y0 - 3
        for x in range(x0, x1 + 1):
            if G.get(x, gy) in "g,;":
                G.set(x, gy, "h")
        G.set(x0 + 12, gy, "g")            # poarta
    # magazie si sere
    G.room(46, 16, 52, 18, "2", "w")
    G.set(49, 18, "D")
    G.fill(22, 8, 23, 9, "h"); G.fill(22, 31, 23, 32, "h")
    # markeri
    G.set(1 + 1, 20, "S"); G.set(2, 21, "S"); G.set(3, 20, "S")
    for (x, y) in ((7, 6), (38, 13), (15, 34), (45, 29), (36, 7)):
        G.set(x, y, "E")
    for (x, y) in ((26, 10), (26, 32), (49, 20), (12, 24), (40, 24)):
        G.set(x, y, "p")
    G.set(15, 12, "O"); G.set(38, 36, "O")
    G.set(10, 10, "c"); G.set(44, 9, "k"); G.set(6, 37, "b")
    G.set(18, 14, "C"); G.set(50, 17, "P"); G.set(34, 37, "K")
    return G


def training():
    """Poligon de tragere: culoare la 10 / 25 / 50 / 100 m, linie de tragere, pista de sprint.
    Tintele sunt create din assets/config/range.ini (nu sunt markeri pe harta)."""
    G = Grid(114, 34, ",")
    # cabine de tragere: separatoare scurte (zid scund) intre culoare, la linia de tragere
    for y in (3, 7, 11, 15, 19, 23, 27):
        for x in range(4, 8):
            G.set(x, y, "h")
    # zona de pregatire (beton) in spatele liniei
    G.fill(1, 1, 3, 28, ".")
    # parapet / val de pamant in spate (piatra)
    G.vwall(110, 1, 32, "5")
    G.vwall(111, 1, 32, "5")
    # pista de sprint (proba de stres), separata de culoare de un zid scund
    G.hwall(1, 70, 29, "h")
    G.set(20, 29, ","); G.set(50, 29, ",")
    G.fill(1, 30, 70, 32, ";")
    # banci / lazi de munitie la linia de tragere
    G.set(2, 6, "c"); G.set(2, 18, "c"); G.set(2, 25, "b")
    G.set(2, 14, "S"); G.set(3, 13, "S")
    G.set(3, 30, "p"); G.set(68, 31, "p")
    return G


def tactical():
    """Teren mare cu mai multe zone: sat (NV), complex industrial (NE), camp deschis cu
    garduri vii (centru), drum lung, cladire fortificata (S). Linii de vedere lungi, 4 obiective."""
    G = Grid(112, 74, "g")
    G.fill(1, 34, 110, 37, ";")            # drumul est-vest
    G.fill(54, 1, 57, 72, ",")             # drumul de pamant nord-sud
    # --- Satul (NV): case mici
    for (x0, y0) in ((4, 4), (20, 4), (4, 18), (20, 18), (36, 10)):
        G.room(x0, y0, x0 + 11, y0 + 9, "5", "w")
        G.vwall(x0 + 6, y0 + 1, y0 + 8, "4")
        G.set(x0 + 6, y0 + 3, "D")
        G.set(x0 + 3, y0 + 9, "D")
        G.set(x0 + 9, y0, "6"); G.set(x0, y0 + 5, "6"); G.set(x0 + 11, y0 + 6, "6")
        G.lamps(x0, y0, x0 + 11, y0 + 9, 4)
    # --- Complexul industrial (NE): hala + depozit + rezervoare
    G.room(64, 4, 104, 26, "1", ".")
    G.vwall(84, 5, 25, "1")
    G.set(84, 10, "D"); G.set(84, 20, "D")
    G.set(64, 15, "D"); G.set(74, 26, "D"); G.set(96, 26, "D")
    for y in (9, 14, 19):
        G.fill(68, y, 80, y, "3")
    for x in (89, 94, 99):
        G.fill(x, 8, x + 1, 22, "3")
    for x in range(66, 104, 5):
        G.set(x, 4, "6")
    G.lamps(64, 4, 104, 26, 5)
    # --- Campul deschis (centru): garduri vii (ziduri scunde) intrerupte
    for y in (42, 50, 58):
        for x in range(4, 50):
            if (x // 6) % 3 != 2:
                G.set(x, y, "h")
    for x in range(62, 108):
        if (x // 7) % 3 != 1:
            G.set(x, 46, "h")
    # --- Cladirea fortificata (S): beton, curte interioara
    G.room(66, 52, 104, 70, "1", ".")
    G.room(76, 56, 94, 66, "1", "s")       # curtea / sala centrala
    G.set(76, 61, "D"); G.set(94, 61, "D"); G.set(85, 56, "D"); G.set(85, 66, "D")
    G.set(66, 60, "D"); G.set(85, 52, "D"); G.set(104, 64, "D")
    for x in range(70, 102, 6):
        G.set(x, 52, "6")
    G.lamps(66, 52, 104, 70, 5)
    # stive de containere pe camp (acoperire la distanta mare)
    for (x, y) in ((30, 46), (14, 62), (40, 66), (90, 40), (100, 30)):
        G.fill(x, y, x + 5, y + 1, "3")
    # markeri
    G.set(2, 35, "S"); G.set(2, 36, "S"); G.set(3, 35, "S")
    for (x, y) in ((9, 8), (40, 14), (25, 22), (78, 15), (100, 12), (92, 20), (70, 60), (100, 68),
                   (85, 63), (35, 54)):
        G.set(x, y, "E")
    for (x, y) in ((55, 20), (55, 50), (20, 46), (80, 45), (40, 36), (100, 36), (60, 66)):
        G.set(x, y, "p")
    G.set(30, 14, "O"); G.set(74, 12, "O"); G.set(85, 61, "O"); G.set(22, 54, "O")
    G.set(8, 26, "c"); G.set(70, 30, "b"); G.set(46, 40, "c"); G.set(80, 68, "k")
    G.set(102, 6, "C"); G.set(68, 68, "P"); G.set(41, 18, "K")
    return G


def main():
    os.makedirs(ROOT, exist_ok=True)
    maps = {"urban": urban(), "industrial": industrial(), "residential": residential(),
            "training": training(), "tactical": tactical()}
    for name, g in maps.items():
        check(g, name)
        with open(os.path.join(ROOT, name + ".txt"), "w", newline="\n") as f:
            f.write(g.text())
    return 0


if __name__ == "__main__":
    sys.exit(main())
