# script_workload.py <base-workload> <out> <cut-field> <terminal> [events-file]
# Keeps the base workload's port-0 pad admissions before <cut-field>, connects port 1
# (same device as port 0) and appends scripted presses. events-file lines:
#   <field> <port> <buttons-hex> [hold-fields=4]     ('#' comments allowed)
# A press admits the button mask at <field> and a release (0) <hold> fields later.
import sys

CYCLES_PER_FIELD = 4920115  # measured from recorded sessions (start N ~ cycle N * this)
NEUTRAL = "0 0 0 0 0 0 1 1 1 0"  # sticks, triggers, caps, deadzone

base, out, cut, terminal = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
events = []
if len(sys.argv) > 5:
    for raw in open(sys.argv[5]):
        raw = raw.split('#')[0].split()
        if raw:
            f, port, buttons = int(raw[0]), int(raw[1]), raw[2]
            hold = int(raw[3]) if len(raw) > 3 else 4
            events.append((f, port, buttons))
            events.append((f + hold, port, '0'))

lines = [l.rstrip('\n') for l in open(base)]
assert lines[-1].startswith('end ')
device = None
body, pads = [], []
for l in lines[:-1]:
    t = l.split()
    if t[0] == 'pad_initial' and t[1] == '0':
        device = t[3]
    if t[0] == 'pad_initial' and t[1] == '1':
        continue  # replaced below
    if t[0] == 'pad':
        if int(t[1]) < cut * CYCLES_PER_FIELD and t[3] == '0':
            pads.append(t)
        continue
    if t[0] == 'terminal_starts':
        l = f'terminal_starts {terminal}'
    body.append(l)
assert device
i = next(k for k, l in enumerate(body) if l.startswith('pad_initial 0'))
body.insert(i + 1, f'pad_initial 1 1 {device} 0 {NEUTRAL}')

# last recorded port-0 analog state is reused so the scripted presses do not jerk the sticks
last0 = ' '.join(pads[-1][7:]) if pads else NEUTRAL
merged = [(int(p[1]), int(p[3]), ' '.join(p[4:])) for p in pads]
merged.append((cut * CYCLES_PER_FIELD, 0, f'1 0 0 {last0}'))
for f, port, buttons in events:
    tail = last0 if port == 0 else NEUTRAL
    merged.append((f * CYCLES_PER_FIELD + 1 + port, port, f'1 0 {buttons} {tail}'))
merged.sort()
padlines = [f'pad {c} {n + 1} {port} {s}' for n, (c, port, s) in enumerate(merged)]
first_hle = next(k for k, l in enumerate(body) if l.startswith('hle '))
body[first_hle:first_hle] = padlines
body.append(f'end {len(body)}')
open(out, 'w').write('\n'.join(body) + '\n')
print(f'{out}: {len(pads)} recorded + {len(events)} scripted admissions, cut {cut}, terminal {terminal}')
