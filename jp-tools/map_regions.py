import pathlib,struct,json,re,collections
P=pathlib.Path('/tmp/fukami-region-analysis'); R=pathlib.Path('/tmp/fukami-jp-source')
def elf(name):
 d=(P/name).read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',d);ss=[struct.unpack_from('<IIIIIIIIII',d,h[6]+i*h[11]) for i in range(h[12])];s=ss[h[13]];names=d[s[4]:s[4]+s[5]]
 return {names[x[0]:].split(b'\0')[0].decode(): (x[3],d[x[4]:x[4]+x[5]]) for x in ss if x[1]!=8}
U=elf('us-SLUS_200.02');J=elf('jp-SLPS_200.01');ub,ud=U['.text'];jb,jd=J['.text']
uw=list(struct.unpack('<%dI'%(len(ud)//4),ud));jw=list(struct.unpack('<%dI'%(len(jd)//4),jd))
def norm(w):
 op=w>>26
 return w if op in (0,28) else w&0xfc000000 if op in (2,3) else w&0xffff0000
un=b''.join(struct.pack('<I',norm(w)) for w in uw);jn=b''.join(struct.pack('<I',norm(w)) for w in jw)
# Unique 12-instruction signatures, vote across surrounding windows.
ix={}
for i in range(0,len(jn)-47,4):
 key=jn[i:i+48];ix[key]=i if key not in ix else -1
votes=collections.defaultdict(collections.Counter)
for i in range(0,len(un)-47,4):
 q=ix.get(un[i:i+48],-1)
 if q>=0:
  for k in range(0,48,4):votes[ub+i+k][jb+q+k]+=1
mapping={a:c.most_common(1)[0][0] for a,c in votes.items() if len(c)==1}
refs=collections.defaultdict(collections.Counter)
for ua,ja in mapping.items():
 ui=(ua-ub)//4;ji=(ja-jb)//4;w=uw[ui];v=jw[ji]
 if w>>26 in (2,3) and v>>26==w>>26:refs[(w&0x3ffffff)*4][(v&0x3ffffff)*4]+=1
 if w>>26!=15 or v>>26!=15:continue
 reg=(w>>16)&31
 for k in range(1,9):
  if ui+k>=len(uw) or ji+k>=len(jw):break
  x,y=uw[ui+k],jw[ji+k]
  if norm(x)!=norm(y):break
  op=x>>26
  if (x>>21)&31==reg and op in (9,13,25,32,33,35,36,37,39,40,41,43,49,57,55,63,30,31):
   low=x&65535;lowj=y&65535
   if op!=13:low=low-65536 if low>=32768 else low;lowj=lowj-65536 if lowj>=32768 else lowj
   a=((w&65535)*65536+low)&0xffffffff;b=((v&65535)*65536+lowj)&0xffffffff
   if 0x200000<=a<0x2000000 and 0x200000<=b<0x2000000:refs[a][b]+=1
  # stop if the upper register is overwritten
  if op==0 and (x>>11)&31==reg or op in (9,13,15,25,32,33,35,36,37,39,49,55) and (x>>16)&31==reg:break
for a,c in refs.items():
 if len(c)==1:
  b=next(iter(c))
  if a not in mapping:mapping[a]=b
(P/'address-map.json').write_text(json.dumps({hex(k):hex(v) for k,v in sorted(mapping.items())},indent=1))
text=(R/'config/rrv.toml').read_text();addresses={int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',text) if 0x200000<=int(x,16)<0x2000000}
missing=sorted(addresses-mapping.keys());print('mapped code instructions',len(votes),'unique',len(mapping),'config addresses',len(addresses),'missing',len(missing));print([hex(x) for x in missing])
(P/'ambiguous-refs.json').write_text(json.dumps({hex(a):{hex(b):n for b,n in c.items()} for a,c in refs.items() if len(c)>1},indent=1))
