import pathlib,json,struct,collections
P=pathlib.Path('/tmp/fukami-region-analysis')
m={int(a,16):int(b,16) for a,b in json.loads((P/'address-map.json').read_text()).items()}
def elf(name):
 d=(P/name).read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',d);ss=[struct.unpack_from('<IIIIIIIIII',d,h[6]+i*h[11]) for i in range(h[12])];s=ss[h[13]];names=d[s[4]:s[4]+s[5]]
 return {names[x[0]:].split(b'\0')[0].decode(): (x[3],d[x[4]:x[4]+x[5]]) for x in ss if x[1]!=8}
u=elf('us-SLUS_200.02');j=elf('jp-SLPS_200.01');ug=struct.unpack_from('<I',u['.reginfo'][1],20)[0];jg=struct.unpack_from('<I',j['.reginfo'][1],20)[0];print('gp',hex(ug),hex(jg))
refs=collections.defaultdict(collections.Counter)
for a,b in m.items():
 if not (0x200000<=a<0x2e1a10 and 0x200000<=b<0x2e06c0):continue
 w=struct.unpack_from('<I',u['.text'][1],a-0x200000)[0];v=struct.unpack_from('<I',j['.text'][1],b-0x200000)[0]
 if w&0xffff0000!=v&0xffff0000 or (w>>21)&31!=28:continue
 if w>>26 not in (9,25,32,33,35,36,37,39,40,41,43,49,57,55,63,30,31):continue
 off=w&65535;off=off-65536 if off>=32768 else off;offj=v&65535;offj=offj-65536 if offj>=32768 else offj
 refs[ug+off][jg+offj]+=1
for a,c in refs.items():
 if len(c)==1:m[a]=next(iter(c))
(P/'address-map.json').write_text(json.dumps({hex(a):hex(b) for a,b in sorted(m.items())},indent=1))
for a in [0x334e94,0x3348ac,0x334ea0,0x334f00,0x334ba4]:print(hex(a),hex(m[a]) if a in m else 'unmapped')
