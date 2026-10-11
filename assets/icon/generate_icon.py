"""Optional authoring tool: Python standard library only; never run by CMake.

Rasterizes the simple rect/polygon SVG source, with alpha and 4x antialiasing.
Replace the source/artwork and checked-in ICO together when branding changes.
"""
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 256)


def contains(shape, x, y):
    if shape.tag.endswith('rect'):
        a = shape.attrib
        left, top, width, height, radius = (float(a[k]) for k in ('x', 'y', 'width', 'height', 'rx'))
        if not (left <= x <= left + width and top <= y <= top + height):
            return False
        cx = min(max(x, left + radius), left + width - radius)
        cy = min(max(y, top + radius), top + height - radius)
        return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2
    points = [tuple(map(float, p.split(','))) for p in shape.attrib['points'].split()]
    inside = False
    for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            inside = not inside
    return inside


def png(size, shapes):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            colors = []
            for sy in range(4):
                for sx in range(4):
                    px, py = (x + (sx + .5) / 4) * 16 / size, (y + (sy + .5) / 4) * 16 / size
                    color = None
                    for shape in shapes:
                        if contains(shape, px, py):
                            color = bytes.fromhex(shape.attrib['fill'][1:])
                    if color is not None:
                        colors.append(color)
            if colors:
                rows.extend(round(sum(c[i] for c in colors) / len(colors)) for i in range(3))
                rows.append(round(len(colors) * 255 / 16))
            else:
                rows.extend((0, 0, 0, 0))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    shapes = list(ET.parse(ROOT / 'Yinamp.svg').getroot())
    frames = [png(size, shapes) for size in SIZES]
    offset = 6 + 16 * len(frames)
    directory = bytearray(struct.pack('<HHH', 0, 1, len(frames)))
    for size, frame in zip(SIZES, frames):
        directory.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(frame), offset))
        offset += len(frame)
    (ROOT / 'Yinamp.ico').write_bytes(directory + b''.join(frames))
    (ROOT / 'Yinamp.png').write_bytes(frames[-1])
