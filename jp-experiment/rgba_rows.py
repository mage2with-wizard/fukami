# rgba_rows.py <out.png> <dir>... : one row per dir of gate4 captures (label = dir name)
import sys, os, re
from PIL import Image, ImageDraw
out, dirs = sys.argv[1], sys.argv[2:]
rows = []
for d in dirs:
    fs = sorted((int(re.match(r'field-(\d+)-', f).group(1)), f) for f in os.listdir(d) if f.endswith('.rgba'))
    row = []
    for n, f in fs:
        w, h = map(int, re.search(r'-(\d+)x(\d+)\.rgba', f).groups())
        im = Image.frombytes('RGBA', (w, h), open(os.path.join(d, f), 'rb').read()).convert('RGB').resize((256, 179))
        ImageDraw.Draw(im).text((4, 160), f"{os.path.basename(d)} {n}", fill=(255, 255, 0)); row.append(im)
    rows.append(row)
cols = max(len(r) for r in rows)
sheet = Image.new('RGB', (cols * 256, len(rows) * 179))
for y, r in enumerate(rows):
    for x, t in enumerate(r): sheet.paste(t, (x * 256, y * 179))
sheet.save(out)
