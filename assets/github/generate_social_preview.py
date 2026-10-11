"""Render the repository card from Yinamp's original vector icon and typography.

Authoring helper only; requires Pillow and Windows Segoe UI fonts.
Run from any directory with Python. Does not modify application assets.
"""

from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).with_name("social-preview.png")
SCALE = 2
SIZE = (1280, 640)
image = Image.new("RGB", tuple(value * SCALE for value in SIZE), "#141722")
draw = ImageDraw.Draw(image)


def box(bounds):
    return tuple(round(value * SCALE) for value in bounds)


def text(position, value, size, color, bold=False):
    font_name = "segoeuib.ttf" if bold else "segoeui.ttf"
    font = ImageFont.truetype(str(Path("C:/Windows/Fonts") / font_name), size * SCALE)
    draw.text(box(position), value, font=font, fill=color, anchor="lt")


# Quiet frame and gold accent echo the original icon's slate/silver/gold palette.
draw.rectangle(box((32, 32, 1247, 607)), outline="#353B50", width=SCALE)
draw.rectangle(box((80, 80, 140, 84)), fill="#F6C858")
text((160, 72), "YINAMP", 18, "#A9ADBE", bold=True)

# Read the original SVG geometry directly; no third-party logos or simulated UI.
icon = ET.parse(ROOT / "assets/icon/Yinamp.svg").getroot()
origin_x, origin_y, unit = 72, 204, 16
for shape in icon:
    kind = shape.tag.rsplit("}", 1)[-1]
    fill = shape.attrib["fill"]
    if kind == "rect":
        x, y, width, height = (float(shape.attrib[key]) for key in ("x", "y", "width", "height"))
        bounds = box((origin_x + x * unit, origin_y + y * unit,
                      origin_x + (x + width) * unit, origin_y + (y + height) * unit))
        draw.rounded_rectangle(bounds, radius=float(shape.attrib.get("rx", 0)) * unit * SCALE, fill=fill)
    elif kind == "polygon":
        points = [tuple(map(float, point.split(","))) for point in shape.attrib["points"].split()]
        draw.polygon([box((origin_x + x * unit, origin_y + y * unit)) for x, y in points], fill=fill)
    else:
        raise ValueError(f"Unsupported original icon element: {kind}")

text((388, 188), "Yinamp", 94, "#F5F4EF", bold=True)
text((394, 314), "Native Windows media player", 34, "#CDD0DC")
draw.line(box((394, 380, 1184, 380)), fill="#353B50", width=SCALE)
text((394, 413), "Classic UI. Native playback. Native YouTube.", 25, "#F6C858")
text((80, 542), "C++23  /  Win32  /  Windows Media Foundation", 20, "#A9ADBE")

image = image.resize(SIZE, Image.Resampling.LANCZOS)
image.save(OUTPUT, optimize=True)
assert image.size == SIZE
assert OUTPUT.stat().st_size < 1_000_000
print(f"{OUTPUT}: {image.width}x{image.height}, {OUTPUT.stat().st_size:,} bytes")
