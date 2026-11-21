"""Boot splash scenes for RNode R8 ColorUI (160x120 art, drawn at 2x).
One scene is shown per boot; the firmware rotates through them.
Keep the top 18 art rows for the title and the bottom 22 rows for the strip.
Writes core/cui_splash_art.h and, with --png DIR, a 1x preview per scene."""
import math, random, sys, os

W, H = 160, 120
HZ = 92  # default horizon row


class Canvas:
    def __init__(self):
        self.g = [[None] * W for _ in range(H)]

    def px(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < H and c: self.g[y][x] = c

    def get(self, x, y):
        return self.g[y][x] if 0 <= x < W and 0 <= y < H else None

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
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for dx in range(w):
                for dy in range(w): self.px(x + dx - w // 2, y + dy - w // 2, c)


# ---------------------------------------------------------------- helpers
SKIES = {
    'space':  ['#04060D', '#060A16', '#09101F', '#0C1529', '#111A35', '#1A1F44', '#2A2350', '#3E2A5C', '#5A3266'],
    'alien':  ['#030806', '#06110C', '#0A1A12', '#0E2418', '#12301F', '#1A3D27', '#244A30', '#2F5838', '#3D6642'],
    'sunset': ['#2B1B4F', '#4B2463', '#7A2E6B', '#B8445E', '#E86A4A', '#F7A440', '#FBC060'],
    'day':    ['#3B7BD9', '#4A8BE6', '#5C9CF0', '#73AEF5', '#8EC2F8', '#A8D4FA'],
    'dawn':   ['#1B2A4A', '#2D3F66', '#4D5A86', '#8A7BA0', '#D39AA0', '#F5C29A', '#FBD9A8'],
}


def sky(cv, theme, hz=HZ):
    bands = SKIES[theme]
    for y in range(hz):
        f = y / hz * (len(bands) - 1); b = int(f); fr = f - b
        for x in range(W):
            c = bands[b]
            if b + 1 < len(bands) and fr > 0.5 and (x + y) % 2 == 0: c = bands[b + 1]
            cv.px(x, y, c)


def stars(cv, n, seed, ymax=80):
    random.seed(seed)
    for _ in range(n):
        x, y = random.randrange(W), random.randrange(ymax)
        r = random.random()
        cv.px(x, y, '#FFFFFF' if r < 0.35 else ('#9FB0C0' if r < 0.8 else '#FFE08A'))


def twinkle(cv, x, y):
    for d in (-1, 1): cv.px(x + d, y, '#9FB0C0'); cv.px(x, y + d, '#9FB0C0')
    cv.px(x, y, '#FFFFFF')


def hills(cv, far, near, base=86):
    for x in range(W):
        h2 = int(base + 3 * math.sin(x / 9.0) + 2 * math.sin(x / 4.1 + 1))
        for y in range(h2, H): cv.px(x, y, far)
        h1 = int(base + 6 + 2 * math.sin(x / 6.0 + 2))
        for y in range(h1, H): cv.px(x, y, near)


def field(cv, top, g1, g2, x0=0, x1=W):
    for y in range(top, H):
        for x in range(x0, x1):
            c = g1
            if (x * 3 + y * 7) % 11 == 0 or ((y - top) % 4 == 0 and x % 3 == 0): c = g2
            cv.px(x, y, c)


def cloud(cv, x, y, w, c='#FFFFFF', c2='#D8E6F5'):
    cv.rect(x, y, w, 3, c); cv.rect(x + 2, y - 2, w - 5, 2, c); cv.rect(x + 4, y - 3, w - 9, 1, c)
    cv.rect(x + 1, y + 3, w - 2, 1, c2)


def sun(cv, cx, cy, r, c='#FFE066'):
    cv.disc(cx, cy, r, c)
    for k in range(8):
        a = k * math.pi / 4
        cv.px(cx + (r + 2) * math.cos(a), cy + (r + 2) * math.sin(a), c)


def moon(cv, cx, cy, r, c='#F2F0DC'):
    cv.disc(cx, cy, r, c, lambda x, y: not ((x - cx - 2) ** 2 + (y - cy + 1) ** 2 <= r * r - 2))


def chirp_beam(cv, x0, y0, x1, y1, bw=6.0, sym=6.0, seed=5, edges='#24486A', pre=6, gaps=True):
    """A LoRa packet as an SDR waterfall shows it, laid along a beam from
    (x0,y0) to (x1,y1): preamble up-chirps (cyan), two sync down-chirps
    (violet), then data up-chirps that wrap (orange). The far end is oldest."""
    dx, dy = x1 - x0, y1 - y0
    dist = math.hypot(dx, dy); ux, uy = dx / dist, dy / dist
    nx, ny = -uy, ux
    d_start, d_end = 3.0, dist - 3.0
    if edges:
        for i in range(int(d_start), int(d_end), 2):
            for side in (-1, 1):
                cv.px(x0 + ux * i + nx * (bw + 1.5) * side, y0 + uy * i + ny * (bw + 1.5) * side, edges)
    random.seed(seed)
    G = [('gap', 0, None)] if gaps else []
    syms = [('up', 0.0, '#3DD6F5')] * pre + G + [('down', 0.0, '#B48CFF')] + G + [('down', 0.0, '#B48CFF')] + G
    while sum(sym * (0.5 if k == 'gap' else 1) for k, _, _ in syms) < (d_end - d_start) - sym:
        syms.append(('up', random.choice([0.15, 0.3, 0.45, 0.6, 0.75, 0.9]), '#FF9F43'))
    pos = d_end
    for kind, off, colr in syms:
        L = sym * (0.5 if kind == 'gap' else 1.0)
        pos -= L
        if pos < d_start - 0.5: break
        if kind == 'gap': continue
        for j in range(0, 71):
            t = j / 70.0
            f = (t + off) % 1.0 if kind == 'up' else 1.0 - t
            if kind == 'up' and off and abs((t + off) - 1.0) < 0.02: continue
            w = (f - 0.5) * 2 * bw
            d = pos + t * L
            cv.px(x0 + ux * d + nx * w, y0 + uy * d + ny * w, colr)


SKIN, HAIR, CAP, JACK, JACK2, PANT, BOOT = '#F0C08A', '#5A3A22', '#FF9F43', '#2E7D4F', '#1F5C39', '#2A3550', '#14141C'


def head(cv, x, y, face=1, mouth=None, cap=True, eye='#14141C'):
    """Head whose top-left is about (x-3, y-6); (x, y) is the chin centre."""
    cv.rect(x - 3, y - 6, 7, 7, SKIN)
    hx = x - 3 if face > 0 else x + 2
    cv.rect(hx, y - 4, 2, 3, HAIR)
    cv.px(x + 2 * face, y - 3, eye)
    if mouth == 'o': cv.px(x + 2 * face, y - 1, '#8A2A2A'); cv.px(x + 1 * face, y - 1, '#8A2A2A')
    if mouth == 'grin': cv.rect(x + (0 if face > 0 else -2), y - 1, 3, 1, '#8A2A2A')
    if cap:
        cv.rect(x - 4, y - 8, 9, 2, CAP)
        cv.rect(x + (4 if face > 0 else -6), y - 7, 3, 1, CAP)


def body(cv, x, y, legs='stand', face=1):
    """Torso + legs; (x, y) = feet baseline centre. Returns the chin point."""
    if legs == 'stand':
        cv.rect(x - 5, y - 2, 4, 2, BOOT); cv.rect(x + 2, y - 2, 4, 2, BOOT)
        cv.rect(x - 4, y - 10, 3, 8, PANT); cv.rect(x + 2, y - 10, 3, 8, PANT)
    elif legs == 'dangle':
        cv.rect(x - 4, y - 10, 3, 7, PANT); cv.rect(x + 2, y - 9, 3, 7, PANT)
        cv.rect(x - 5, y - 3, 4, 2, BOOT); cv.rect(x + 2, y - 2, 4, 2, BOOT)
    elif legs == 'run':
        cv.line(x - 2, y - 10, x - 6, y - 3, PANT, 3); cv.rect(x - 9, y - 3, 4, 2, BOOT)
        cv.line(x + 2, y - 10, x + 5, y - 2, PANT, 3); cv.rect(x + 4, y - 2, 4, 2, BOOT)
    elif legs == 'sit':      # sitting on an edge, legs over it
        cv.rect(x - 4, y - 3, 9, 3, PANT)
        cv.rect(x + 3 * face, y, 3, 6, PANT); cv.rect(x + 3 * face - (1 if face < 0 else 0), y + 6, 4, 2, BOOT)
        y = y + 7
    top = y - 10 if legs != 'sit' else y - 10
    cv.rect(x - 4, top - 2, 9, 3, PANT)
    cv.rect(x - 5, top - 12, 11, 11, JACK)
    cv.rect(x + (3 if face > 0 else -5), top - 12, 3, 11, JACK2)
    cv.rect(x - 5, top - 2, 11, 1, '#14141C')
    return x, top - 12


def handheld(cv, x, y):
    """Small radio with a whip antenna, held at (x, y)."""
    cv.rect(x - 1, y - 2, 3, 5, '#3A4452'); cv.px(x, y - 1, '#4ADE80')
    for i in range(6): cv.px(x + 1, y - 3 - i, '#C8CED6')
    cv.px(x + 1, y - 9, '#FF5050')
    return x + 1, y - 9


def house(cv, x, y, w, h, roof_h, wall='#E8D3A8', wall2='#C9AF82', roof='#B23A3A', roof2='#8A2A2A', win='#FFD36E'):
    cv.rect(x, y - h, w, h, wall); cv.rect(x + w - 4, y - h, 4, h, wall2)
    cx = x + w / 2
    for j in range(roof_h):
        half = (j + 1) * (w / 2 + 4) / roof_h
        for i in range(int(cx - half), int(cx + half)):
            cv.px(i, y - h - roof_h + j, roof if (i + j) % 5 else roof2)
    cv.rect(x + 6, y - h + 6, 8, 7, win); cv.rect(x + 9, y - h + 6, 1, 7, wall2); cv.rect(x + 6, y - h + 9, 8, 1, wall2)
    cv.rect(x + w - 18, y - 13, 7, 13, '#6B3E1F'); cv.px(x + w - 13, y - 7, '#FFE08A')
    return cx, y - h - roof_h


def rnode_box(cv, x, y, label=True):
    """Weatherproof RNode enclosure, top-left (x, y), 10x8, with LED."""
    cv.rect(x, y, 10, 8, '#D9DEE5'); cv.rect(x, y + 7, 10, 1, '#9AA4B0'); cv.rect(x + 9, y, 1, 8, '#B8C0CA')
    cv.rect(x + 2, y + 2, 4, 2, '#2A3550')
    cv.px(x + 7, y + 2, '#4ADE80')
    cv.px(x + 7, y + 5, '#3DD6F5')


def bang(cv, x, y, c='#FFE08A'):
    cv.rect(x, y, 2, 5, c); cv.rect(x, y + 6, 2, 2, c)


# ---------------------------------------------------------------- scenes
def scene_launcher():
    cv = Canvas(); sky(cv, 'space'); stars(cv, 120, 11)
    for p in [(18, 40), (132, 70), (60, 14)]: twinkle(cv, *p)
    pcx, pcy, pr = 30, 28, 9
    cv.disc(pcx, pcy, pr, '#B48CFF')
    cv.disc(pcx, pcy, pr, '#8A63D9', lambda x, y: (x - pcx) + (y - pcy) * 0.6 > 3)
    cv.disc(pcx - 3, pcy - 3, 2.2, '#D9C2FF')
    for t in range(0, 360, 2):
        a = math.radians(t)
        x = pcx + 15 * math.cos(a); y = pcy + 4 * math.sin(a) - 0.25 * 15 * math.cos(a)
        if math.sin(a) < 0 and (x - pcx) ** 2 + (y - pcy) ** 2 <= pr * pr: continue
        cv.px(x, y, '#FFE08A')
    sx, sy = 132, 24
    satellite(cv, sx, sy)
    hills(cv, '#1B2440', '#141B30')
    top = 84
    bluff(cv, top)
    gx, gy = 34, top
    body(cv, gx, gy)
    ang = math.radians(-32); ox, oy = gx + 1.5, gy - 21
    for t10 in range(-90, 170):
        t = t10 / 10.0
        cx, cy = ox + t * math.cos(ang), oy + t * math.sin(ang)
        for w in (-2, -1, 0, 1, 2):
            c = '#5E6B7A' if abs(w) < 2 else '#3A4452'
            if abs(t - 4) < 0.6 or abs(t - 12) < 0.6: c = '#FFE08A'
            cv.px(cx - w * math.sin(ang), cy + w * math.cos(ang), c)
    mx, my = ox + 17 * math.cos(ang), oy + 17 * math.sin(ang)
    for w in range(-3, 4): cv.px(mx - w * math.sin(ang), my + w * math.cos(ang), '#C8CED6')
    cv.rect(gx + 2, gy - 19, 7, 3, JACK2); cv.rect(gx + 8, gy - 21, 2, 3, SKIN)
    cv.rect(gx - 5, gy - 20, 3, 6, JACK)
    head(cv, gx, gy - 22)
    backpack(cv, gx - 8, gy - 21)
    chirp_beam(cv, mx, my, sx - 6, sy + 8)
    return cv, 'Sending packets to the stars'


def satellite(cv, sx, sy):
    cv.rect(sx - 2, sy - 2, 5, 5, '#C8CED6'); cv.rect(sx - 1, sy - 1, 3, 3, '#8A94A6')
    for i in range(1, 9):
        for j in (-1, 0, 1):
            cv.px(sx - 3 - i, sy + j - (i // 4), '#3DD6F5' if (i + j) % 3 else '#2A7FA8')
            cv.px(sx + 3 + i, sy + j + (i // 4), '#3DD6F5' if (i + j) % 3 else '#2A7FA8')
    for i in range(4): cv.px(sx - 3 - i // 2, sy + 3 + i, '#C8CED6')
    cv.disc(sx - 6, sy + 8, 2.2, '#E6EDF3'); cv.px(sx - 6, sy + 8, '#FF9F43')


def bluff(cv, top, x1=84):
    for x in range(0, x1):
        edge = top + (0 if x < x1 - 14 else (x - (x1 - 14)) ** 2 // 6)
        for y in range(edge, H):
            c = '#3A2E3E' if (x * 3 + y * 5) % 9 else '#2A2030'
            if (y - edge) % 7 == 0 and x % 4 < 2: c = '#4A3A4C'
            cv.px(x, y, c)
        for y in range(edge, min(edge + 3, H)): cv.px(x, y, '#2F6B3E' if (x + y) % 3 else '#3F8A4E')
        if x % 5 == 0: cv.px(x, edge - 1, '#3F8A4E')


def backpack(cv, x, y):
    cv.rect(x, y, 3, 8, '#8A2A2A')
    for i in range(9): cv.px(x + 1 - i // 4, y - 1 - i, '#C8CED6')
    cv.px(x - 1, y - 10, '#FF5050')


def scene_ufo():
    cv = Canvas(); sky(cv, 'alien'); stars(cv, 90, 21, 70)
    moon(cv, 24, 30, 6)
    hills(cv, '#16261C', '#0F1C14', base=84)
    field(cv, 94, '#1D3B2A', '#152C1F')
    # fence
    for x in range(4, 70, 8): cv.rect(x, 88, 1, 6, '#5E4A3A')
    cv.rect(2, 89, 68, 1, '#5E4A3A'); cv.rect(2, 92, 68, 1, '#5E4A3A')
    # distant repeater tower on the hill, left
    tx, ty = 18, 66
    for i in range(18):
        cv.px(tx - i // 6, ty + i, '#8A94A6'); cv.px(tx + i // 6, ty + i, '#8A94A6')
        if i % 4 == 0: cv.rect(tx - i // 6, ty + i, 2 * (i // 6) + 1, 1, '#8A94A6')
    cv.px(tx, ty - 1, '#FF5050'); cv.px(tx, ty - 2, '#FF5050')
    # UFO
    ux, uy = 104, 30
    # tractor beam (lighten the background in a trapezoid)
    for y in range(uy + 4, 96):
        half = 6 + (y - uy - 4) * 0.33
        for x in range(int(ux - half), int(ux + half) + 1):
            if (x + y) % 2 == 0: cv.px(x, y, '#C8F57A' if (x * 7 + y) % 9 else '#E8FFB0')
            elif cv.get(x, y) and cv.get(x, y) not in ('#C8F57A',): cv.px(x, y, '#5A7A3A')
    cv.ellipse(ux, uy, 18, 4.5, '#9AA4B0'); cv.ellipse(ux, uy - 1, 16, 2.5, '#C8CED6')
    cv.ellipse(ux, uy - 4, 7, 4.5, '#7FE6FA'); cv.ellipse(ux - 2, uy - 6, 2.5, 1.5, '#E6FDFF')
    for i, c in enumerate(['#FF5050', '#FFE08A', '#4ADE80', '#3DD6F5', '#B48CFF', '#FF9F43']):
        cv.px(ux - 14 + i * 5.5, uy + 2, c)
    # cow, also going up
    cx, cy = 122, 70
    cv.rect(cx, cy, 11, 6, '#F2F2F2'); cv.rect(cx + 2, cy + 1, 3, 2, '#14141C'); cv.rect(cx + 7, cy + 3, 2, 2, '#14141C')
    cv.rect(cx + 11, cy - 1, 4, 4, '#F2F2F2'); cv.px(cx + 14, cy, '#14141C'); cv.rect(cx + 13, cy + 2, 2, 1, '#F5A3B5')
    for lx in (cx + 1, cx + 8): cv.rect(lx, cy + 6, 1, 3, '#F2F2F2')
    # the guy floating up, still transmitting
    gx, gy = 98, 76
    chin = body(cv, gx, gy, legs='dangle', face=-1)
    cv.rect(gx - 9, gy - 24, 3, 6, JACK)                        # left arm up
    cv.rect(gx + 6, gy - 22, 3, 6, JACK2)                       # right arm out
    head(cv, gx, gy - 22, face=-1, mouth='o', cap=False)
    cv.rect(gx - 2, gy - 37, 5, 2, CAP)                         # cap flying off
    tip = handheld(cv, gx - 8, gy - 26)
    chirp_beam(cv, tip[0] - 1, tip[1] + 1, tx + 3, ty + 2, bw=4.5, sym=5.0, seed=8, pre=4)
    bang(cv, gx + 10, gy - 40)
    return cv, 'Take me to your transport node'


def scene_roof():
    cv = Canvas(); sky(cv, 'sunset')
    sun(cv, 128, 60, 9, '#FFD36E')
    cloud(cv, 14, 30, 16, '#F59A7A', '#C9707A'); cloud(cv, 96, 24, 13, '#F59A7A', '#C9707A')
    hills(cv, '#5B2E4E', '#3E2140', base=84)
    field(cv, 96, '#4E6B3A', '#3B5230')
    hx, base = 34, 97
    peak = house(cv, hx, base, 58, 26, 20)
    # the guy on the right slope, slipping
    rx = 82; ry = base - 26 - 8           # roof edge height around here
    gx, gy = 80, 66
    # one leg on the roof, one kicking out
    cv.rect(gx - 4, gy - 10, 3, 9, PANT); cv.rect(gx - 5, gy - 2, 4, 2, BOOT)
    cv.line(gx + 3, gy - 9, gx + 9, gy - 5, PANT, 3); cv.rect(gx + 9, gy - 7, 4, 2, BOOT)
    cv.rect(gx - 4, gy - 12, 9, 3, PANT)
    # torso leaning back
    for j in range(11):
        cv.rect(gx - 5 + j // 4, gy - 22 + j, 10, 1, JACK)
    head(cv, gx + 4, gy - 22, face=1, mouth='o')
    # arms flailing
    cv.line(gx + 6, gy - 19, gx + 12, gy - 26, JACK2, 2)
    cv.line(gx - 4, gy - 19, gx - 9, gy - 23, JACK, 2)
    # the mast he's holding, tipping, with an RNode box on it
    cv.line(gx - 9, gy - 23, gx - 17, gy - 36, '#C8CED6', 1)
    rnode_box(cv, gx - 24, gy - 40)
    for i in range(6): cv.px(gx - 20 - i // 3, gy - 41 - i, '#C8CED6')
    # wobble lines + a sliding shingle + fallen ladder
    for k in range(3): cv.px(gx + 14 + k, gy - 30 - k, '#FFFFFF'); cv.px(gx + 15 + k, gy - 26 - k, '#FFFFFF')
    cv.rect(gx + 12, gy + 4, 4, 2, '#8A2A2A')
    for i in range(22):
        cv.px(110 + i, 95 - i // 4, '#C99A5A'); cv.px(110 + i, 92 - i // 4, '#C99A5A')
        if i % 4 == 0: cv.line(110 + i, 92 - i // 4, 110 + i, 95 - i // 4, '#C99A5A')
    bang(cv, gx + 8, gy - 38)
    # small chirp burst from the RNode anyway
    chirp_beam(cv, gx - 24, gy - 44, gx - 56, gy - 40, bw=3.0, sym=4.5, seed=3, pre=3, edges=None, gaps=False)
    return cv, 'Almost... got... the mount...'


def scene_solar():
    cv = Canvas(); sky(cv, 'day')
    sun(cv, 140, 30, 6)
    cloud(cv, 10, 28, 18); cloud(cv, 54, 36, 14)
    hills(cv, '#5A8F6A', '#3F7A4E', base=84)
    field(cv, 94, '#4CAF50', '#3D9142')
    # distant repeater on the far hill
    tx, ty = 138, 72
    for i in range(14):
        cv.px(tx - i // 5, ty + i, '#6B7A8A'); cv.px(tx + i // 5, ty + i, '#6B7A8A')
    cv.px(tx, ty - 1, '#FF5050')
    # pole, RNode box, solar panel, antenna
    px_ = 62
    cv.rect(px_, 44, 2, 52, '#8A94A6')
    # solar panel on an arm
    for j in range(10):
        for i in range(16):
            x, y = px_ + 6 + i, 50 + j - i // 3
            cv.px(x, y, '#2A4F8A' if (i % 4 and j % 3) else '#6FA0D8')
    cv.rect(px_ + 2, 56, 6, 1, '#8A94A6')
    rnode_box(cv, px_ - 11, 52)
    cv.rect(px_ - 2, 55, 2, 1, '#8A94A6')
    # cable from the panel to the box
    for i in range(10): cv.px(px_ + 6 - i, 64 + (i % 2), '#14141C')
    # antenna whip from the top of the box
    for i in range(26): cv.px(px_ - 6, 51 - i, '#E6EDF3')
    cv.px(px_ - 6, 25, '#FF5050')
    # a bird on the antenna tip
    bx, by = px_ - 8, 25
    cv.rect(bx, by - 2, 4, 2, '#5A3A22'); cv.px(bx + 4, by - 2, '#FF9F43'); cv.px(bx + 3, by - 3, '#5A3A22'); cv.px(bx + 3, by - 2, '#14141C')
    # chirps out to the repeater
    chirp_beam(cv, px_ - 3, 27, tx - 2, ty + 1, bw=3.5, sym=5.0, seed=12)
    # the guy, proud, thumbs up
    gx, gy = 26, 96
    body(cv, gx, gy)
    head(cv, gx, gy - 22, mouth='grin')
    cv.rect(gx + 6, gy - 22, 3, 4, JACK2); cv.rect(gx + 7, gy - 26, 2, 4, SKIN)    # thumb up
    cv.rect(gx - 8, gy - 21, 3, 7, JACK)
    return cv, 'Off grid. Always listening.'


def scene_balloon():
    cv = Canvas(); sky(cv, 'day')
    cloud(cv, 8, 40, 20); cloud(cv, 120, 46, 16); cloud(cv, 70, 26, 12)
    hills(cv, '#5A8F6A', '#3F7A4E', base=86)
    field(cv, 95, '#4CAF50', '#3D9142')
    # weather balloon drifting away with an RNode payload
    bx, by = 112, 32
    cv.disc(bx, by, 9, '#F2F2F2'); cv.disc(bx - 3, by - 3, 3, '#FFFFFF'); cv.disc(bx + 4, by + 3, 4, '#D8DEE6')
    cv.rect(bx - 1, by + 9, 3, 2, '#D8DEE6')
    for i in range(14): cv.px(bx + (i // 5), by + 11 + i, '#E6EDF3')
    rnode_box(cv, bx - 3, by + 25)
    for i in range(5): cv.px(bx + 5, by + 24 - i, '#C8CED6')
    # chirps beaming back down toward home
    chirp_beam(cv, bx - 3, by + 30, 54, 84, bw=4.0, sym=5.0, seed=2, pre=4)
    # the guy running after it, arms up
    gx, gy = 34, 96
    body(cv, gx, gy, legs='run')
    head(cv, gx + 1, gy - 22, face=1, mouth='o')
    cv.line(gx + 4, gy - 21, gx + 10, gy - 30, JACK2, 2); cv.line(gx - 4, gy - 21, gx - 8, gy - 29, JACK, 2)
    # a cut string on the ground stake
    cv.rect(50, 92, 1, 4, '#8A94A6')
    for i in range(6): cv.px(51 + i, 92 - i // 2, '#E6EDF3')
    cv.px(gx + 14, gy - 36, '#FFFFFF'); cv.px(gx + 16, gy - 38, '#FFFFFF')
    return cv, 'Range test: going great'


def scene_fishing():
    cv = Canvas(); sky(cv, 'dawn', hz=78)
    sun(cv, 120, 76, 7, '#FFD9A0')
    cloud(cv, 20, 30, 16, '#F5D0C0', '#D9A9A8'); cloud(cv, 80, 40, 12, '#F5D0C0', '#D9A9A8')
    # far shore
    for x in range(W):
        h = int(74 + 2 * math.sin(x / 7.0)); cv.rect(x, h, 1, 78 - h + 1, '#3A3B5A')
    # lake with sun reflection
    for y in range(78, H):
        for x in range(W):
            c = '#2D4F7A' if (x + 2 * y) % 7 else '#3D6A9A'
            if y < 96 and abs(x - 120) < 10 - (y - 78) * 0.4 and y % 2 == 0 and x % 3: c = '#F5C29A'
            cv.px(x, y, c)
    # dock
    cv.rect(0, 82, 52, 3, '#8B5A2B'); cv.rect(0, 85, 52, 1, '#5E3A1E')
    for x in (6, 22, 38, 50): cv.rect(x, 86, 2, 10, '#5E3A1E')
    # the guy sitting on the end of the dock
    gx, gy = 40, 82
    cv.rect(gx - 4, gy - 3, 9, 3, PANT)
    cv.rect(gx + 3, gy, 3, 7, PANT); cv.rect(gx + 3, gy + 7, 4, 2, BOOT)
    cv.rect(gx - 5, gy - 14, 11, 11, JACK); cv.rect(gx + 3, gy - 14, 3, 11, JACK2)
    head(cv, gx, gy - 14, mouth='grin')
    # rod: from his hands up and out, bent by a big catch
    hx, hy = gx + 8, gy - 10
    cv.rect(gx + 5, gy - 11, 4, 3, JACK2)
    pts = [(hx + i * 3.2, hy - i * 3.6 + (i * i) * 0.22) for i in range(10)]
    for (a, b), (c, d) in zip(pts, pts[1:]): cv.line(a, b, c, d, '#C99A5A')
    tipx, tipy = pts[-1]
    # line down to the catch: a LoRa chirp packet leaping out of the water
    catchx, catchy = 112, 64
    cv.line(tipx, tipy, catchx - 6, catchy - 4, '#E6EDF3')
    chirp_beam(cv, catchx - 16, catchy + 8, catchx + 16, catchy - 10, bw=5.0, sym=4.5, seed=4, pre=3, edges=None, gaps=False)
    # splash
    for (x, y) in [(100, 77), (103, 74), (108, 73), (120, 75), (124, 77), (115, 72)]:
        cv.px(x, y, '#E6FDFF'); cv.px(x, y + 1, '#7FE6FA')
    # a bucket of packets
    cv.rect(10, 76, 7, 6, '#9AA4B0'); cv.rect(10, 76, 7, 1, '#C8CED6')
    cv.px(12, 75, '#FF9F43'); cv.px(14, 74, '#3DD6F5'); cv.px(15, 75, '#B48CFF')
    return cv, 'Caught a 255-byte packet'



# ---------------------------------------------------------------- science scenes
def dashed_ellipse_between(cv, x0, y0, x1, y1, ry, c, step=2, frac=1.0):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    a = math.hypot(x1 - x0, y1 - y0) / 2; th = math.atan2(y1 - y0, x1 - x0)
    n = 0
    for k in range(0, 720):
        t = math.radians(k / 2)
        ex, ey = a * math.cos(t), ry * frac * math.sin(t)
        x = cx + ex * math.cos(th) - ey * math.sin(th); y = cy + ex * math.sin(th) + ey * math.cos(th)
        n += 1
        if (n // 3) % step == 0: cv.px(x, y, c)


def mast(cv, x, ytop, ybot, c='#C8CED6'):
    cv.rect(x, ytop, 1, ybot - ytop, c); cv.px(x, ytop - 1, '#FF5050')


def tree(cv, x, ybot, h, r):
    cv.rect(x - 1, ybot - h, 3, h, '#6B3E1F')
    cv.disc(x, ybot - h, r, '#2F6B3E'); cv.disc(x - r // 2, ybot - h + 2, r - 2, '#3F8A4E'); cv.disc(x + r // 2, ybot - h - 1, r - 3, '#4CAF50')


def scene_fresnel():
    cv = Canvas(); sky(cv, 'day'); sun(cv, 18, 26, 5)
    cloud(cv, 60, 26, 14)
    field(cv, 90, '#4CAF50', '#3D9142')
    # two hills with an antenna on each
    for x in range(W):
        hl = 58 + int(((x - 18) / 30.0) ** 2 * 20) if x < 50 else 200
        hr = 56 + int(((x - 142) / 30.0) ** 2 * 20) if x > 110 else 200
        for y in range(min(hl, hr), 96): cv.px(x, y, '#3F7A4E' if (x + y) % 5 else '#356B44')
    ax, ay, bx, by = 18, 40, 142, 38
    mast(cv, ax, ay, 59); mast(cv, bx, by, 57)
    rnode_box(cv, ax - 5, 50); rnode_box(cv, bx - 5, 48)
    # line of sight + first Fresnel zone (exaggerated vertically) + the 60% line
    for i in range(0, 124, 3): cv.px(ax + i, ay + (by - ay) * i / 124, '#FFFFFF')
    dashed_ellipse_between(cv, ax, ay, bx, by, 16, '#FFE08A')
    dashed_ellipse_between(cv, ax, ay, bx, by, 16, '#FF9F43', step=3, frac=0.6)
    # a tree growing up into the zone, and the guy up it with a saw
    tree(cv, 80, 95, 40, 8)
    gx, gy = 88, 64
    cv.rect(gx - 1, gy - 9, 5, 7, JACK); head(cv, gx + 1, gy - 9, face=-1, mouth='grin')
    cv.rect(gx - 1, gy - 2, 2, 5, PANT); cv.rect(gx + 2, gy - 2, 2, 5, PANT)
    # saw on the top branch + sawdust
    cv.rect(gx - 9, gy - 8, 7, 2, '#C8CED6'); cv.rect(gx - 2, gy - 8, 2, 3, '#8A2A2A')
    for (x, y) in [(gx - 8, gy - 4), (gx - 6, gy - 2), (gx - 9, gy), (gx - 7, gy + 3)]: cv.px(x, y, '#E8D3A8')
    return cv, 'Keep 60% of Fresnel zone clear'


def scene_noise():
    cv = Canvas(); sky(cv, 'day', hz=36)
    cloud(cv, 100, 22, 14)
    random.seed(17)
    # sea; the top layer is the noise floor: random static
    for y in range(36, H):
        for x in range(W):
            if y < 50:
                r = random.random()
                c = '#9FB0C0' if r < 0.25 else ('#E6EDF3' if r < 0.38 else ('#5C7EA0' if r < 0.7 else '#33506E'))
            else:
                c = '#1E4E78' if (x * 5 + y * 3) % 13 else '#245A88'
                if y > 70: c = '#173E62' if (x * 5 + y * 3) % 13 else '#1C4870'
            cv.px(x, y, c)
    # sand
    for x in range(W):
        h = int(92 + 2 * math.sin(x / 8.0))
        for y in range(h, H): cv.px(x, y, '#C9AF82' if (x + y) % 4 else '#B8995F')
    # an RNode on a buoy, top left, sending down into the sea
    cv.rect(14, 30, 8, 6, '#FF5050'); cv.rect(14, 33, 8, 1, '#FFFFFF')
    rnode_box(cv, 13, 23); mast(cv, 17, 20, 23)
    # chirps passing clean below the static
    chirp_beam(cv, 20, 58, 104, 70, bw=5.0, sym=6.0, seed=9, edges='#2D6E9E')
    # the diver, receiving
    gx, gy = 118, 80
    for j in range(14): cv.rect(gx - 4 + j // 7, gy - 14 + j, 10, 1, '#1F2A36')     # wetsuit, swimming pose
    cv.rect(gx + 6, gy - 10, 8, 3, '#1F2A36'); cv.rect(gx + 13, gy - 11, 4, 5, '#FFE08A')   # leg + fin
    cv.rect(gx - 9, gy - 17, 7, 6, SKIN); cv.rect(gx - 9, gy - 16, 7, 3, '#7FE6FA'); cv.rect(gx - 9, gy - 17, 7, 1, '#14141C')  # mask
    cv.rect(gx - 2, gy - 22, 2, 7, '#FFE08A')                                              # snorkel
    cv.rect(gx - 4, gy - 14, 4, 6, '#9AA4B0')                                              # tank
    tip = handheld(cv, gx - 12, gy - 9)
    for (x, y) in [(gx - 5, gy - 27), (gx - 3, gy - 32), (gx - 6, gy - 36)]: cv.disc(x, y, 1, '#BFE9FF')
    return cv, 'LoRa decodes at -20 dB SNR'


def scene_sf_race():
    cv = Canvas(); sky(cv, 'day'); sun(cv, 138, 26, 5)
    cloud(cv, 30, 28, 14)
    hills(cv, '#5A8F6A', '#3F7A4E', base=60)
    field(cv, 66, '#4CAF50', '#3D9142')
    # two lanes
    for x in range(0, W, 4): cv.px(x, 78, '#E6EDF3'); cv.px(x, 96, '#E6EDF3')
    cv.rect(8, 62, 1, 34, '#FFFFFF')                                   # start
    for j in range(0, 34, 2):                                         # finish (checkered)
        cv.px(150, 62 + j, '#14141C'); cv.px(151, 63 + j, '#14141C'); cv.px(151, 62 + j, '#FFFFFF'); cv.px(150, 63 + j, '#FFFFFF')
    # top lane: SF7 hare, fast short chirps, then asleep under a tree
    chirp_beam(cv, 10, 71, 92, 71, bw=3.5, sym=3.5, seed=1, pre=8, edges=None, gaps=True)
    tree(cv, 112, 76, 14, 7)
    hx, hy = 100, 74
    cv.rect(hx, hy - 4, 9, 4, '#D8C8B0'); cv.rect(hx + 8, hy - 7, 4, 4, '#D8C8B0')
    cv.rect(hx + 9, hy - 12, 1, 5, '#D8C8B0'); cv.rect(hx + 11, hy - 11, 1, 4, '#D8C8B0')
    cv.px(hx + 10, hy - 5, '#14141C')
    for (x, y, k) in [(hx + 14, hy - 12, 2), (hx + 17, hy - 16, 3)]:
        cv.rect(x, y, k, 1, '#FFFFFF'); cv.line(x + k - 1, y, x, y + k - 1, '#FFFFFF'); cv.rect(x, y + k - 1, k, 1, '#FFFFFF')
    # bottom lane: SF9 tortoise, chirps 4x longer, carrying the RNode
    tx, ty = 124, 93
    chirp_beam(cv, 10, 88, tx - 6, 88, bw=3.5, sym=14.0, seed=6, pre=2, edges=None, gaps=True)
    cv.ellipse(tx, ty - 4, 7, 4, '#3F7A4E'); cv.ellipse(tx, ty - 5, 5, 2.5, '#5A8F6A')
    cv.rect(tx + 6, ty - 4, 4, 3, '#8FBF6A'); cv.px(tx + 8, ty - 4, '#14141C')
    cv.rect(tx - 5, ty - 1, 2, 2, '#8FBF6A'); cv.rect(tx + 3, ty - 1, 2, 2, '#8FBF6A')
    rnode_box(cv, tx - 5, ty - 16); mast(cv, tx - 2, ty - 22, ty - 16)
    return cv, '+1 SF: 2x airtime, +2.5 dB'


def pigeon(cv, x, y):
    cv.ellipse(x, y, 5, 3, '#9AA4B0'); cv.rect(x + 4, y - 4, 3, 3, '#9AA4B0')
    cv.px(x + 6, y - 3, '#14141C'); cv.px(x + 7, y - 2, '#FF9F43')
    cv.rect(x + 4, y - 1, 2, 1, '#4ADE80')          # neck shine
    for i in range(6): cv.px(x - 3 + i, y - 4 - (i % 3), '#C8CED6')   # wing up
    cv.rect(x - 7, y, 3, 2, '#7A8494')               # tail


def scene_pigeon():
    cv = Canvas(); sky(cv, 'sunset'); sun(cv, 30, 70, 8, '#FFD36E')
    cloud(cv, 90, 30, 14, '#F59A7A', '#C9707A')
    hills(cv, '#5B2E4E', '#3E2140', base=84)
    field(cv, 94, '#4E6B3A', '#3B5230')
    # dotted flight path from the guy to the pigeon
    for i in range(0, 60, 3): cv.px(40 + i, 70 - i * 0.55 + math.sin(i / 5) * 2, '#FFE08A')
    pigeon(cv, 108, 36)
    # little packet in its feet
    cv.rect(104, 40, 5, 4, '#E6EDF3'); cv.line(104, 40, 106, 42, '#9AA4B0'); cv.line(108, 40, 106, 42, '#9AA4B0')
    # the guy, arms up, just released it
    gx, gy = 34, 96
    body(cv, gx, gy)
    head(cv, gx, gy - 22, mouth='grin')
    cv.line(gx + 5, gy - 21, gx + 9, gy - 30, JACK2, 2); cv.line(gx - 4, gy - 21, gx - 8, gy - 29, JACK, 2)
    backpack(cv, gx - 8, gy - 21)
    # a pigeon coop with an antenna on it
    cv.rect(122, 78, 18, 16, '#8B5A2B'); cv.rect(120, 76, 22, 3, '#5E3A1E')
    cv.rect(128, 84, 6, 5, '#2A1E16'); mast(cv, 138, 64, 76)
    # a snail with an antenna, also a valid link
    sx, sy = 74, 94
    cv.disc(sx, sy - 3, 3, '#C99A5A'); cv.px(sx, sy - 3, '#8B5A2B'); cv.rect(sx - 5, sy - 1, 9, 2, '#D8C8B0')
    cv.rect(sx + 3, sy - 4, 1, 3, '#D8C8B0'); cv.px(sx + 3, sy - 6, '#FF5050'); cv.rect(sx + 3, sy - 5, 1, 1, '#C8CED6')
    return cv, 'Reticulum works at 5 bits/sec'


def scene_polarization():
    cv = Canvas(); sky(cv, 'dawn')
    sun(cv, 80, 70, 6, '#FFD9A0')
    hills(cv, '#3A3B5A', '#2A2B48', base=80)
    field(cv, 92, '#4E6B3A', '#3B5230')
    # repeater tower, right: vertical antenna
    tx = 136
    for i in range(36):
        cv.px(tx - i // 9, 56 + i, '#8A94A6'); cv.px(tx + i // 9, 56 + i, '#8A94A6')
        if i % 6 == 0: cv.rect(tx - i // 9, 56 + i, 2 * (i // 9) + 1, 1, '#8A94A6')
    mast(cv, tx, 34, 56)
    for k in range(3): cv.px(tx - 3 - k * 2, 38 + k, '#3DD6F5'); cv.px(tx + 3 + k * 2, 38 + k, '#3DD6F5')
    # the guy holding his whip sideways, like a lance
    gx, gy = 28, 92
    body(cv, gx, gy)
    head(cv, gx, gy - 22, mouth='grin')
    cv.rect(gx + 5, gy - 18, 6, 2, JACK2)
    cv.rect(gx + 9, gy - 18, 6, 3, '#3A4452'); cv.px(gx + 11, gy - 17, '#4ADE80')
    cv.rect(gx + 15, gy - 17, 26, 1, '#C8CED6'); cv.px(gx + 41, gy - 17, '#FF5050')
    # his chirps, fading out before they arrive
    random.seed(3)
    chirp_beam(cv, gx + 42, gy - 17, 98, 62, bw=4.0, sym=5.0, seed=7, pre=3, edges=None, gaps=False)
    for i in range(40):
        x = 100 + i * 0.75 + random.randint(-2, 2); y = 61 - i * 0.35 + random.randint(-3, 3)
        if random.random() < 0.5 - i / 90: cv.px(x, y, random.choice(['#3DD6F5', '#B48CFF', '#FF9F43']))
    # question mark over the repeater
    cv.rect(tx + 6, 26, 3, 1, '#FFE08A'); cv.px(tx + 9, 27, '#FFE08A'); cv.px(tx + 8, 28, '#FFE08A'); cv.px(tx + 7, 29, '#FFE08A'); cv.px(tx + 7, 31, '#FFE08A')
    return cv, 'Cross-polarized: 20+ dB lost'


def scene_yagi():
    cv = Canvas(); sky(cv, 'day'); cloud(cv, 50, 24, 12); cloud(cv, 104, 32, 16)
    # far mountains
    for x in range(W):
        h = int(64 + 10 * abs(math.sin(x / 21.0)) + 3 * math.sin(x / 5.0))
        for y in range(h, 90): cv.px(x, y, '#6F8FB0' if y > h + 2 else '#E6EDF3')
    hills(cv, '#5A8F6A', '#3F7A4E', base=86)
    field(cv, 94, '#4CAF50', '#3D9142')
    # tripod + a big yagi pointing right
    gx, gy = 24, 94
    cv.line(gx, gy, gx + 8, gy - 22, '#5E6B7A'); cv.line(gx + 16, gy, gx + 8, gy - 22, '#5E6B7A'); cv.line(gx + 8, gy, gx + 8, gy - 22, '#5E6B7A')
    by = gy - 24
    cv.rect(gx - 2, by, 40, 1, '#C8CED6')                    # boom
    for i, L in enumerate([10, 8, 7, 7, 6, 6, 5, 5]):        # reflector, driven, directors
        x = gx - 2 + i * 5
        cv.rect(x, by - L // 2, 1, L, '#E6EDF3' if i else '#FF9F43')
    cv.rect(gx + 2, by + 1, 1, 4, '#14141C')                 # feed line
    # the guy pointing at it proudly
    hx = gx - 12
    body(cv, hx, gy)
    head(cv, hx, gy - 22, mouth='grin')
    cv.rect(hx + 6, gy - 20, 7, 2, JACK2)
    # the beam reaches twice as far: flags at 1x and 2x
    chirp_beam(cv, gx + 40, by, 150, by - 6, bw=3.5, sym=5.0, seed=14, pre=6, edges='#24486A')
    for (fx, c) in [(94, '#FFE08A'), (148, '#4ADE80')]:
        cv.rect(fx, by + 4, 1, 14, '#E6EDF3'); cv.rect(fx + 1, by + 4, 5, 3, c)
    cv.rect(96, by + 9, 1, 1, '#14141C')                     # "1" on the first flag, sort of
    return cv, '+6 dB = double the distance'


SCENES = [scene_launcher, scene_ufo, scene_roof, scene_solar, scene_balloon, scene_fishing,
          scene_fresnel, scene_noise, scene_sf_race, scene_pigeon, scene_polarization, scene_yagi]


# ---------------------------------------------------------------- export
def rgb565(h):
    v = int(h[1:], 16); r, gg, b = v >> 16, (v >> 8) & 255, v & 255
    return ((r & 0xF8) << 8) | ((gg & 0xFC) << 3) | (b >> 3)


def main():
    out = 'core/cui_splash_art.h'
    png_dir = None
    args = sys.argv[1:]
    if '--png' in args:
        png_dir = args[args.index('--png') + 1]; os.makedirs(png_dir, exist_ok=True)
    lines = ['// Generated by tools/gen_splash.py -- boot splash scenes (160x120, drawn at 2x).',
             '#pragma once', '#include <stdint.h>', 'namespace cui {']
    names = []
    total = 0
    for n, fn in enumerate(SCENES):
        cv, caption = fn()
        cols, idx = [], []
        for y in range(H):
            for x in range(W):
                c = cv.g[y][x] or '#000000'
                if c not in cols: cols.append(c)
                idx.append(cols.index(c))
        assert len(cols) <= 256, (fn.__name__, len(cols))
        lines.append(f'static const uint16_t kSplashPal{n}[{len(cols)}] = {{' + ', '.join(f'0x{rgb565(c):04x}' for c in cols) + '};')
        lines.append(f'static const uint8_t kSplashPix{n}[{W}*{H}] = {{')
        for y in range(H): lines.append('  ' + ','.join(str(v) for v in idx[y * W:(y + 1) * W]) + ',')
        lines.append('};')
        names.append((n, caption))
        total += W * H + 2 * len(cols)
        if png_dir:
            from PIL import Image
            im = Image.new('RGB', (W, H))
            im.putdata([tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) for c in (cv.g[y][x] or '#000000' for y in range(H) for x in range(W))])
            im.save(os.path.join(png_dir, f'art{n}.png'))
        print(f'{fn.__name__:16s} colors={len(cols):3d}  "{caption}"')
    lines.append(f'static const int kSplashCount = {len(SCENES)};')
    lines.append('static const uint8_t* const kSplashPix[] = {' + ', '.join(f'kSplashPix{n}' for n, _ in names) + '};')
    lines.append('static const uint16_t* const kSplashPal[] = {' + ', '.join(f'kSplashPal{n}' for n, _ in names) + '};')
    lines.append('static const char* const kSplashCaption[] = {' + ', '.join('"' + c + '"' for _, c in names) + '};')
    lines.append('}  // namespace cui')
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('flash bytes', total)


if __name__ == '__main__':
    main()
