from pathlib import Path
import re,collections
R=Path('/tmp/fukami-jp-source');p=R/'jp-analysis/rrv-jp.toml';s=p.read_text();j=(R/'jp-analysis/analyzer.toml').read_text()
jp={int(a,16):n for n,a in re.findall(r'"([^"@]+)@0x([0-9a-fA-F]+)"',j)}
groups=collections.defaultdict(list)
for n,a in re.findall(r'"([^"@]+)@0x([0-9a-fA-F]+)"',s):groups[int(a,16)].append(n)
removed=[]
for a,names in groups.items():
 if len(names)>1:
  keep=jp.get(a)
  if keep not in names:raise RuntimeError((hex(a),names))
  for n in names:
   if n!=keep:
    s=re.sub(r'^\s*"'+re.escape(n)+r'@0x'+format(a,'08X')+r'",?\n','',s,flags=re.M);removed.append((n,hex(a)))
p.write_text(s);(R/'jp-analysis/regen.toml').write_text(s.replace('/generated/rr5/output/','/jp-analysis/regen/'))
print('removed conflicting stub routes',removed)
