import pathlib,json,re,tomllib
P=pathlib.Path('/tmp/fukami-region-analysis');R=pathlib.Path('/tmp/fukami-jp-source')
m={int(a,16):int(b,16) for a,b in json.loads((P/'address-map.json').read_text()).items()}
us=(R/'config/rrv.toml').read_text();jp=(R/'jp-analysis/analyzer.toml').read_text()
usnames={n:int(a,16) for n,a in re.findall(r'"([^"@]+)@0x([0-9A-Fa-f]+)"',us)}
jpnames={n:int(a,16) for n,a in re.findall(r'"([^"@]+)@0x([0-9A-Fa-f]+)"',jp)}
for n,a in usnames.items():
 if n in jpnames:m[a]=jpnames[n]
# Leave no unresolved stub addresses; force funcs unmatched are omitted for initial experiment.
unknown=[]
def transform(mt):
 a=int(mt[1],16)
 return '0x'+format(m[a],'08X') if a in m else mt[0]
lines=[]
for line in us.splitlines():
 if line.lstrip().startswith('#'):lines.append(line);continue
 addrs=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line)]
 missing=[a for a in addrs if 0x200000<=a<0x2e1b00 and a not in m]
 if missing:
  unknown.append(line)
  # remove individual quoted force addresses, or entire MMIO entry
  if line.startswith('"'):continue
  for a in missing:line=re.sub(r'"0x0*'+format(a,'x')+'"\s*,?', '',line,flags=re.I)
  if '@0x' in line:continue
 lines.append(re.sub(r'0x([0-9A-Fa-f]+)',transform,line))
text='\n'.join(lines)+'\n'
text=re.sub(r'^input = .*$', 'input = "/tmp/fukami-region-analysis/jp-SLPS_200.01"',text,flags=re.M)
text=re.sub(r'^output = .*$', 'output = "/tmp/fukami-jp-source/generated/rr5/output/"',text,flags=re.M)
text=re.sub(r'^\s*"rand@[^\n]+\n','',text,flags=re.M)
tomllib.loads(text)
(R/'jp-analysis/rrv-jp.toml').write_text(text);(P/'jp-omitted-config.txt').write_text('\n'.join(unknown))
(P/'address-map.json').write_text(json.dumps({hex(k):hex(v) for k,v in sorted(m.items())},indent=1))
print('omitted',len(unknown));print('\n'.join(unknown))
