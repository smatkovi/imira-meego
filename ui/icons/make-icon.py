#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Baut das Startsymbol in MeeGos eigener Symbolform.

Harmattan-Symbole sind nicht frei geformt: jedes Standardsymbol ist auf
denselben abgerundeten "Squircle" geschnitten, und ein Symbol mit eigener
Kontur -- der Kreis des Sailfish-Originals etwa -- fällt zwischen ihnen auf
dem Startbildschirm sofort als fremd auf. Die Silhouette wird deshalb nicht
nachgebaut, sondern wörtlich genommen: der Alphakanal eines Standardsymbols
(/usr/share/themes/blanco/meegotouch/icons/icon-l-*.png, 80x80, 82 % Deckung)
liegt hier als mask-icon-l.png daneben.

Das Zeichen selbst ist das von Imira -- Fernseher mit Funkwellen -- aber hier
gleich für den Squircle gezeichnet statt für einen Kreis: so läuft keine Kante
in die alte Kontur und wird vom Schnitt abgehackt.

Alles entsteht bei 320 px (4x) und wird erst am Ende auf 80 und 64 verkleinert;
bei 80 px gezeichnet franst die Kurve aus.

    python3 make-icon.py        # -> icon-80.png, icon-64.png
"""
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
BIG = 320                      # 4x die 80 px des Startbildschirms
S = BIG / 172.0                # das Original ist 172 px breit


def gradient(size, c0, c1):
    """Linearer Verlauf von links oben nach rechts unten."""
    im = Image.new("RGB", (size, size))
    px = im.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2.0 * (size - 1))
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(c0, c1))
    return im


def wave_gradient(size, c0, c1):
    """Verlauf von links unten nach rechts oben, für die Wellen."""
    im = Image.new("RGB", (size, size))
    px = im.load()
    for y in range(size):
        for x in range(size):
            t = (x + (size - 1 - y)) / (2.0 * (size - 1))
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(c0, c1))
    return im


def main():
    # --- Silhouette: der Alphakanal des Standardsymbols, auf 320 gezogen ---
    mask = Image.open(os.path.join(HERE, "mask-icon-l.png")).convert("RGBA")
    sil = mask.split()[-1].resize((BIG, BIG), Image.LANCZOS)

    # --- Grund ---
    base = gradient(BIG, (0x1e, 0x6f, 0x8e), (0x0d, 0x2b, 0x45))

    # --- Zeichen: Fernseher und Wellen, alles in den Maßen des Originals ---
    art = Image.new("RGBA", (BIG, BIG), (0, 0, 0, 0))
    d = ImageDraw.Draw(art)
    white = (0xf4, 0xf7, 0xfa, 255)

    def sc(v):
        return v * S

    # Fernseherrahmen (im Original rect 34,46 104x68, Radius 7, Strich 8)
    x0, y0, x1, y1 = sc(34), sc(46), sc(34 + 104), sc(46 + 68)
    d.rounded_rectangle([x0, y0, x1, y1], radius=sc(7), outline=white,
                        width=int(round(sc(8))))
    # Standfuß
    d.line([sc(66), sc(128), sc(106), sc(128)], fill=white,
           width=int(round(sc(8))))
    d.ellipse([sc(66) - sc(4), sc(128) - sc(4), sc(66) + sc(4), sc(128) + sc(4)],
              fill=white)
    d.ellipse([sc(106) - sc(4), sc(128) - sc(4), sc(106) + sc(4), sc(128) + sc(4)],
              fill=white)

    # Wellen: zwei Viertelbögen und ein Punkt, aus der linken unteren Ecke
    # des Bildschirms (im Original um 32 nach oben versetzt)
    wav = Image.new("L", (BIG, BIG), 0)
    wd = ImageDraw.Draw(wav)
    dy = -32
    for r in (40, 26):
        # Bogen von oben nach rechts, Mittelpunkt (46, 102+r) bzw. (46,116+r)
        cx, cy = sc(46), sc((102 if r == 40 else 116) + r + dy)
        rr = sc(r)
        wd.arc([cx - rr, cy - rr, cx + rr, cy + rr], start=270, end=360,
               fill=255, width=int(round(sc(9))))
    cx, cy = sc(49), sc(139 + dy)
    rr = sc(8)
    wd.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=255)
    art.paste(wave_gradient(BIG, (0xff, 0xb3, 0x5c), (0xff, 0x8a, 0x3d)),
              (0, 0), wav)

    icon = base.convert("RGBA")
    icon.alpha_composite(art)

    # --- Schnitt auf die Silhouette ---
    icon.putalpha(sil)

    for size, name in ((80, "icon-80.png"), (64, "icon-64.png")):
        icon.resize((size, size), Image.LANCZOS).save(os.path.join(HERE, name))
        print("geschrieben:", name)


if __name__ == "__main__":
    main()
