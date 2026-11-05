# Rendert die Symbole fuer Shore als kantengeglaettete Sprites mit vier
# Alpha-Stufen. Die Uhr faerbt sie ueber die 2-Bit-Palette ein
# (gbitmap_get_palette), deshalb kommt nur Schwarz in vier Deckungsstufen vor.
#
# - weather~round/rect.png: zwoelf Wettersymbole nebeneinander, aus der
#   Weather-Icons-Schrift (Erik Flowers, SIL OFL 1.1) oder -- ohne Schrift --
#   verkleinert aus den groesseren Sprites von Your Style Light.
# - steps~round/rect.png: zwei Fussabdruecke, mit PIL gezeichnet.
#
# Aufruf aus dem Projektordner:
#   python tools/make_icons.py --ttf <weathericons-regular-webfont.ttf>
#   python tools/make_icons.py --sheet ../yourstyle_light/resources/images
import argparse
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

# Reihenfolge = Zustandscode, muss zu COND_* in shore.c und index.js passen
GLYPHS = [
    0xf00d,  # 0  klar, Tag            wi-day-sunny
    0xf02e,  # 1  klar, Nacht          wi-night-clear
    0xf002,  # 2  heiter, Tag          wi-day-cloudy
    0xf086,  # 3  heiter, Nacht        wi-night-alt-cloudy
    0xf013,  # 4  bewoelkt             wi-cloudy
    0xf014,  # 5  Nebel                wi-fog
    0xf01c,  # 6  Niesel               wi-sprinkle
    0xf019,  # 7  Regen                wi-rain
    0xf01a,  # 8  Schauer              wi-showers
    0xf01b,  # 9  Schnee               wi-snow
    0xf0b5,  # 10 Schneeregen/Eisregen wi-sleet
    0xf01e,  # 11 Gewitter             wi-thunderstorm
]

# Kantenlaenge je Symbol in Pixeln, ~round = gabbro, ~rect = emery.
# Muss zu WEATHER_ICON / STEPS_ICON in shore.c passen.
WEATHER_SIZES = {'round': 34, 'rect': 28}
STEPS_SIZES = {'round': 26, 'rect': 22}

BOLDEN = 0.6
MAX_GROW = 1.3

OUT = 'resources/images'


def quantize(alpha):
    # auf vier Stufen 0/85/170/255 runden und als schwarzes RGBA ablegen
    quant = alpha.point(lambda v: min(3, (v + 42) // 85) * 85)
    rgba = Image.new('RGBA', alpha.size, (0, 0, 0, 0))
    rgba.putalpha(quant)
    return rgba


def weather_from_ttf(font_path, cell):
    big = ImageFont.truetype(font_path, 400)
    crops = []
    for cp in GLYPHS:
        img = Image.new('L', (700, 700), 0)
        ImageDraw.Draw(img).text((100, 100), chr(cp), font=big, fill=255)
        crops.append(img.crop(img.getbbox()))
    scale = (cell - 2) / max(max(c.size) for c in crops)
    grow = int(BOLDEN / scale) | 1
    sheet = Image.new('L', (cell * len(GLYPHS), cell), 0)
    for i, img in enumerate(crops):
        pad = grow
        padded = Image.new('L', (img.size[0] + 2 * pad, img.size[1] + 2 * pad), 0)
        padded.paste(img, (pad, pad))
        img = padded.filter(ImageFilter.MaxFilter(grow))
        f = min(scale * MAX_GROW, cell / max(img.size))
        w, h = max(1, round(img.size[0] * f)), max(1, round(img.size[1] * f))
        img = img.resize((w, h), Image.LANCZOS)
        sheet.paste(img, (i * cell + (cell - w) // 2, (cell - h) // 2))
    return quantize(sheet)


def weather_from_sheet(path, cell):
    src = Image.open(path).getchannel('A')
    n = len(GLYPHS)
    src_cell = src.size[0] // n
    sheet = Image.new('L', (cell * n, cell), 0)
    for i in range(n):
        icon = src.crop((i * src_cell, 0, (i + 1) * src_cell, src_cell))
        sheet.paste(icon.resize((cell, cell), Image.LANCZOS), (i * cell, 0))
    return quantize(sheet)


def steps_icon(cell):
    # zwei Fussabdruecke, Sohle plus Ferse, der rechte etwas weiter vorn
    s = 8
    img = Image.new('L', (128 * s, 128 * s), 0)
    d = ImageDraw.Draw(img)
    for dx, dy in ((0, 18), (58, 0)):
        d.ellipse([(10 + dx) * s, (8 + dy) * s, (50 + dx) * s, (68 + dy) * s], fill=255)
        d.ellipse([(16 + dx) * s, (76 + dy) * s, (44 + dx) * s, (106 + dy) * s], fill=255)
    img = img.crop(img.getbbox())
    f = (cell - 1) / max(img.size)
    w, h = max(1, round(img.size[0] * f)), max(1, round(img.size[1] * f))
    img = img.resize((w, h), Image.LANCZOS)
    out = Image.new('L', (cell, cell), 0)
    out.paste(img, ((cell - w) // 2, (cell - h) // 2))
    return quantize(out)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--ttf', help='Weather-Icons-Schrift')
    ap.add_argument('--sheet', help='Ordner mit weather~round.png/weather~rect.png')
    args = ap.parse_args()

    for plat, cell in WEATHER_SIZES.items():
        if args.ttf:
            img = weather_from_ttf(args.ttf, cell)
        elif args.sheet:
            img = weather_from_sheet(os.path.join(args.sheet, 'weather~%s.png' % plat), cell)
        else:
            continue
        img.save(os.path.join(OUT, 'weather~%s.png' % plat))
        print('weather', plat, cell)

    for plat, cell in STEPS_SIZES.items():
        steps_icon(cell).save(os.path.join(OUT, 'steps~%s.png' % plat))
        print('steps', plat, cell)
