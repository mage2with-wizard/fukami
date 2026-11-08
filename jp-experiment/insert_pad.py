# insert_pad.py <in-workload> <out-workload> <field:buttonhex>... : insert a press (and release 15 fields later)
# of port 0 at each field, renumber pad sequences, fix the end count. ~4920116 EE cycles per field.
import sys
src, dst, evs = sys.argv[1], sys.argv[2], sys.argv[3:]
cut = None
if evs and evs[0].startswith('--cut='):
    cut = int(evs[0][6:]) * 4920116; evs = evs[1:]
lines = [l.rstrip('\n') for l in open(src)]
assert lines[-1].startswith('end ')
body = lines[:-1]
pads = [l.split() for l in body if l.startswith('pad ')]
other = [l for l in body if not l.startswith('pad ')]
tmpl = pads[0][3:]  # port + sample
if cut is not None: pads = [p for p in pads if int(p[1]) < cut]
for ev in evs:
    if '=' in ev:  # field=buttons: set the full button state, no automatic release
        f, b = ev.split('='); seq = ((int(f) * 4920116, b),)
    else:
        f, b = ev.split(':'); f = int(f); seq = ((f * 4920116, b), ((f + 15) * 4920116, '0'))
    for cyc, btn in seq:
        s = list(tmpl); s[3] = btn
        pads.append(['pad', str(cyc), '0'] + s)
pads.sort(key=lambda p: int(p[1]))
for i, p in enumerate(pads, 1): p[2] = str(i)
# keep original placement: pad lines go where the first pad line was
first = next(i for i, l in enumerate(body) if l.startswith('pad '))
head = [l for l in body[:first] if not l.startswith('pad ')]
tail = [l for l in body[first:] if not l.startswith('pad ')]
out = head + [' '.join(p) for p in pads] + tail
out.append(f'end {len(out)}')
open(dst, 'w').write('\n'.join(out) + '\n')
