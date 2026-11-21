"""Notification art for ColorUI pop-ups, in the same pixel style as the boot
splashes. Writes core/cui_popup_art.h.

  Cards (120x48 art, drawn at 2x = 240x96): welcome-to-a-city, message-for-you
  Icons (20x20 art, drawn at 2x = 40x40):   new node, host linked, host lost,
                                             GPS fix, GPS lost, interference,
                                             battery low
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import gen_splash as G


class Art:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.g = [[None] * w for _ in range(h)]

    def px(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h and c: self.g[y][x] = c

    def rect(self, x, y, w, h, c):
        for j in range(int(y), int(y + h)):
            for i in range(int(x), int(x + w)): self.px(i, j, c)

    def disc(self, cx, cy, r, c, cond=lambda x, y: True):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and cond(x, y): self.px(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1: self.px(x, y, c)

    def line(self, x0, y0, x1, y1, c, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            for dx in range(w):
                for dy in range(w): self.px(x0 + (x1 - x0) * t + dx - w // 2, y0 + (y1 - y0) * t + dy - w // 2, c)

    def get(self, x, y):
        return self.g[y][x] if 0 <= x < self.w and 0 <= y < self.h else None


def sky_bands(a, bands, hz):
    for y in range(hz):
        f = y / hz * (len(bands) - 1); b = int(f); fr = f - b
        for x in range(a.w):
            c = bands[b]
            if b + 1 < len(bands) and fr > 0.5 and (x + y) % 2 == 0: c = bands[b + 1]
            a.px(x, y, c)


# ------------------------------------------------------------------ cards
def card_welcome():
    """Road into town at golden hour: a green city-limits sign (the city name
    is drawn onto it in real text by the firmware) and the guy's car with a
    whip antenna. Sign face: x 58..113, y 8..27 (art pixels)."""
    a = Art(120, 48)
    sky_bands(a, ['#3B6BC9', '#5C8EE0', '#8FB4EE', '#F7C68A', '#FBD9A8'], 30)
    G.cloud(a, 6, 10, 12); G.cloud(a, 34, 6, 9)
    # distant town skyline
    for (x, w, h) in [(4, 6, 10), (11, 4, 14), (16, 7, 8), (24, 5, 12), (30, 3, 16), (34, 6, 9), (42, 5, 11)]:
        a.rect(x, 30 - h, w, h, '#6F7FA8')
        for j in range(30 - h + 2, 29, 3):
            for i in range(x + 1, x + w - 1, 2): a.px(i, j, '#FFE08A')
    # ground + road in perspective
    for y in range(30, 48):
        for x in range(120):
            a.px(x, y, '#4CAF50' if (x * 3 + y * 7) % 11 else '#3D9142')
    for y in range(30, 48):
        k = (y - 30) / 18
        half = 6 + k * 34
        cx = 30 + k * 10
        for x in range(int(cx - half), int(cx + half)): a.px(x, y, '#4A4F5C')
        if y % 4 < 2: a.px(cx, y, '#FFE08A'); a.px(cx + 1, y, '#FFE08A')
    # the car (side view), whip antenna with a little chirp burst
    cx, cy = 16, 42
    a.rect(cx, cy - 5, 22, 5, '#FF9F43'); a.rect(cx + 5, cy - 9, 11, 4, '#FF9F43')
    a.rect(cx + 6, cy - 8, 4, 3, '#7FD3FF'); a.rect(cx + 11, cy - 8, 4, 3, '#7FD3FF')
    a.disc(cx + 5, cy, 2.5, '#14141C'); a.disc(cx + 17, cy, 2.5, '#14141C')
    a.px(cx + 5, cy, '#9AA4B0'); a.px(cx + 17, cy, '#9AA4B0')
    for i in range(10): a.px(cx + 19, cy - 6 - i, '#C8CED6')
    a.px(cx + 19, cy - 16, '#FF5050')
    a.px(cx + 12, cy - 8, G.SKIN)    # driver
    for k, r in enumerate((3, 5, 7)):
        for t in range(-6, 7):
            ang = -math.pi / 2 + t * 0.12
            a.px(cx + 19 + r * math.cos(ang) + 3, cy - 16 + r * math.sin(ang), ['#3DD6F5', '#B48CFF', '#FF9F43'][k] if abs(t) < 5 else None)
    # the city-limits sign on two posts
    a.rect(64, 28, 2, 14, '#8A94A6'); a.rect(106, 28, 2, 14, '#8A94A6')
    a.rect(57, 7, 58, 22, '#FFFFFF')
    a.rect(58, 8, 56, 20, '#1E7A46')
    a.rect(59, 9, 54, 1, '#FFFFFF'); a.rect(59, 26, 54, 1, '#FFFFFF')
    a.rect(59, 9, 1, 18, '#FFFFFF'); a.rect(112, 9, 1, 18, '#FFFFFF')
    return a


def card_message():
    """A carrier pigeon landing on a mailbox, flag up, sealed envelope."""
    a = Art(120, 48)
    sky_bands(a, ['#2D3F66', '#4D5A86', '#8A7BA0', '#D39AA0', '#F5C29A'], 36)
    G.stars(a, 0, 1) if False else None
    for y in range(36, 48):
        for x in range(120): a.px(x, y, '#3F7A4E' if (x + y) % 5 else '#356B44')
    # mailbox on a post
    mx, my = 20, 20
    a.rect(mx + 7, my + 12, 3, 16, '#6B3E1F')
    a.rect(mx, my, 18, 12, '#3D6AC9'); a.ellipse(mx + 9, my, 9, 4, '#3D6AC9')
    a.rect(mx, my + 3, 1, 9, '#2A4F8A'); a.rect(mx + 17, my + 1, 1, 11, '#2A4F8A')
    a.rect(mx + 18, my - 4, 2, 9, '#FF5050'); a.rect(mx + 20, my - 4, 4, 3, '#FF5050')   # flag up
    # envelope sticking out
    a.rect(mx - 6, my + 4, 10, 7, '#F2F2F2')
    a.line(mx - 6, my + 4, mx - 1, my + 8, '#9AA4B0'); a.line(mx + 3, my + 4, mx - 1, my + 8, '#9AA4B0')
    a.rect(mx - 2, my + 8, 2, 2, '#FF5050')                                              # wax seal
    # pigeon perched on top
    px, py = mx + 6, my - 6
    a.ellipse(px, py, 5, 3, '#9AA4B0'); a.rect(px + 4, py - 4, 3, 3, '#9AA4B0')
    a.px(px + 6, py - 3, '#14141C'); a.px(px + 7, py - 2, '#FF9F43'); a.rect(px + 4, py - 1, 2, 1, '#4ADE80')
    a.rect(px - 8, py, 3, 2, '#7A8494')
    # LoRa chirps arriving from the right (preamble, sync, data)
    G.chirp_beam(a, 118, 14, 50, 22, bw=4.0, sym=5.0, seed=31, pre=4, edges=None)
    # a little lock over the envelope: end-to-end encrypted
    lx, ly = mx - 12, my - 2
    a.rect(lx, ly + 3, 6, 5, '#FFE08A'); a.rect(lx + 1, ly, 1, 3, '#FFE08A'); a.rect(lx + 4, ly, 1, 3, '#FFE08A'); a.rect(lx + 1, ly, 4, 1, '#FFE08A')
    a.px(lx + 2, ly + 5, '#5A3A22'); a.px(lx + 3, ly + 5, '#5A3A22')
    return a


# ------------------------------------------------------------------ icons
def icon_node():
    """The guy waving hello."""
    a = Art(20, 20)
    a.disc(10, 10, 9.5, '#1E3A55')
    a.rect(6, 12, 8, 8, G.JACK); a.rect(11, 12, 3, 8, G.JACK2)
    a.rect(7, 5, 6, 6, G.SKIN); a.rect(7, 6, 2, 3, G.HAIR); a.px(11, 7, '#14141C')
    a.rect(6, 3, 8, 2, G.CAP); a.rect(12, 4, 3, 1, G.CAP)
    a.line(14, 13, 17, 7, G.JACK2, 2); a.rect(16, 5, 2, 2, G.SKIN)
    a.px(18, 3, '#FFFFFF'); a.px(19, 5, '#FFFFFF'); a.px(17, 2, '#FFFFFF')
    return a


def icon_host(lost=False):
    """Laptop plugged into an RNode box; cable cut + spark when lost."""
    a = Art(20, 20)
    a.disc(10, 10, 9.5, '#3A1E1E' if lost else '#1E3A2A')
    a.rect(2, 5, 9, 6, '#9AA4B0'); a.rect(3, 6, 7, 4, '#3DD6F5' if not lost else '#33414F')
    a.rect(1, 11, 11, 2, '#C8CED6')
    a.rect(13, 9, 6, 5, '#D9DEE5'); a.px(17, 10, '#4ADE80' if not lost else '#FF5050')
    a.rect(16, 4, 1, 5, '#C8CED6'); a.px(16, 3, '#FF5050')
    if lost:
        a.line(6, 14, 9, 16, '#14141C'); a.line(12, 16, 15, 14, '#14141C')
        a.px(10, 15, '#FFE08A'); a.px(11, 16, '#FFE08A'); a.px(10, 17, '#FF9F43')
    else:
        a.line(6, 14, 9, 16, '#14141C'); a.line(9, 16, 15, 14, '#14141C')
    return a


def icon_gps(lost=False):
    a = Art(20, 20)
    a.disc(10, 10, 9.5, '#1E2A4A')
    cx, cy = 10, 9
    a.rect(cx - 2, cy - 2, 5, 5, '#C8CED6'); a.rect(cx - 1, cy - 1, 3, 3, '#8A94A6')
    for i in range(1, 6):
        for j in (-1, 0, 1):
            a.px(cx - 2 - i, cy + j, '#3DD6F5' if (i + j) % 3 else '#2A7FA8')
            a.px(cx + 2 + i, cy + j, '#3DD6F5' if (i + j) % 3 else '#2A7FA8')
    if lost:
        a.line(4, 3, 16, 16, '#FF5050', 2); a.line(16, 3, 4, 16, '#FF5050', 2)
    else:
        for k, r in enumerate((4, 6)):
            for t in range(-4, 5):
                ang = math.pi / 2 + t * 0.18
                a.px(cx + r * math.cos(ang), cy + 3 + r * math.sin(ang), '#4ADE80')
    return a


def icon_interference():
    a = Art(20, 20)
    a.disc(10, 10, 9.5, '#3A2A10')
    pts = [(12, 2), (6, 10), (10, 10), (7, 18), (15, 8), (11, 8), (13, 2)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]): a.line(x0, y0, x1, y1, '#FFE08A', 2)
    a.px(3, 5, '#FF5050'); a.px(16, 15, '#FF5050'); a.px(4, 14, '#FF9F43')
    return a


def icon_battery():
    a = Art(20, 20)
    a.disc(10, 10, 9.5, '#3A1E1E')
    a.rect(3, 6, 13, 8, '#E6EDF3'); a.rect(16, 8, 2, 4, '#E6EDF3')
    a.rect(4, 7, 11, 6, '#14141C'); a.rect(5, 8, 2, 4, '#FF5050')
    return a


CARDS = [('Welcome', card_welcome), ('Message', card_message)]
ICONS = [('Node', icon_node), ('HostUp', lambda: icon_host(False)), ('HostLost', lambda: icon_host(True)),
         ('GpsFix', lambda: icon_gps(False)), ('GpsLost', lambda: icon_gps(True)),
         ('Intf', icon_interference), ('Batt', icon_battery)]


def export(name, a, lines):
    cols, idx = [None], []                # index 0 = transparent
    for y in range(a.h):
        for x in range(a.w):
            c = a.g[y][x]
            if c is None: idx.append(0); continue
            if c not in cols: cols.append(c)
            idx.append(cols.index(c))
    assert len(cols) <= 256
    pal = [0] + [G.rgb565(c) for c in cols[1:]]
    lines.append(f'static const uint16_t kPop{name}Pal[{len(pal)}] = {{' + ', '.join(f'0x{v:04x}' for v in pal) + '};')
    lines.append(f'static const uint8_t kPop{name}[{a.w}*{a.h}] = {{')
    for y in range(a.h): lines.append('  ' + ','.join(str(v) for v in idx[y * a.w:(y + 1) * a.w]) + ',')
    lines.append('};')
    return a.w * a.h + 2 * len(pal)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'core/cui_popup_art.h'
    lines = ['// Generated by tools/gen_popups.py -- notification art. Palette index 0 = transparent.',
             '#pragma once', '#include <stdint.h>', 'namespace cui {']
    total = 0
    for n, fn in CARDS: total += export(n, fn(), lines)
    for n, fn in ICONS: total += export(n, fn(), lines)
    lines.append('static const int kPopCardW = 120, kPopCardH = 48, kPopIconW = 20, kPopIconH = 20;')
    lines.append('}  // namespace cui')
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('popup art bytes', total)


if __name__ == '__main__':
    main()
