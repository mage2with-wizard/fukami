# Contact sheet from gate4 capture .rgba files: rgba_sheet.py <captures-dir> <out.png> [cols]
import sys, os, re
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]; cols = int(sys.argv[3]) if len(sys.argv) > 3 else 6
fs = sorted((int(re.match(r'field-(\d+)-', f).group(1)), f) for f in os.listdir(d) if f.endswith('.rgba'))
tiles = []
for n, f in fs:
    w, h = map(int, re.search(r'-(\d+)x(\d+)\.rgba', f).groups())
    im = Image.frombytes('RGBA', (w, h), open(os.path.join(d, f), 'rb').read()).convert('RGB').resize((320, 224))
    ImageDraw.Draw(im).text((4, 4), str(n), fill=(255, 0, 0)); tiles.append(im)
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 320, rows * 224))
for i, t in enumerate(tiles): sheet.paste(t, ((i % cols) * 320, (i // cols) * 224))
sheet.save(out)
