# pad_jsonl_to_workload.py <base-workload> <pad.jsonl> <out-workload> [terminal_starts]
# Turns a recorded session (fukami.ini [game] logging = true -> sessions/<id>/pad.jsonl) into a
# replay workload: the base workload's header and HLE costs, the recorded pad admissions, a
# terminal (default: last recorded start + 1200 fields), and the fresh card the session saw
# (start the replay from a copy of sessions/<id>/mc-start).
import json, sys

base, jsonl, out = sys.argv[1:4]
lines = [l.rstrip('\n') for l in open(base)]
assert lines[-1].startswith('end ')
body = [l for l in lines[:-1] if not l.startswith('pad ') and not l.startswith('terminal_starts ')
        and not l.startswith('rtc_source ')]  # host_boot RTC is only allowed without a terminal
pads, last_start = [], 0
for raw in open(jsonl):
    raw = raw.strip()
    if not raw:
        continue
    e = json.loads(raw)
    pads.append(f"pad {e['cycle']} {e['sequence']} {e['port']} {e['sample']}")
    last_start = max(last_start, int(e.get('start', 0)))
terminal = int(sys.argv[4]) if len(sys.argv) > 4 else last_start + 1200
body.insert(2, f"terminal_starts {terminal}")
first_hle = next(i for i, l in enumerate(body) if l.startswith('hle '))
body[first_hle:first_hle] = pads
body.append(f"end {len(body)}")
open(out, 'w').write('\n'.join(body) + '\n')
print(f"{out}: {len(pads)} pad admissions, terminal_starts {terminal}")
