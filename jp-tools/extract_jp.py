import pathlib,json,hashlib
P=pathlib.Path('/tmp/fukami-jp-source/jp-run/disc');P.mkdir(parents=True,exist_ok=True)
files=json.load(open('/tmp/fukami-region-analysis/files.json'))['jp']
with open(__import__('sys').argv[1],'rb') as f:
 for name,meta in files.items():
  f.seek(meta['lba']*2352);left=meta['size'];h=hashlib.sha256()
  with (P/name).open('wb') as out:
   while left:
    raw=f.read(min(512,(left+2047)//2048)*2352)
    data=b''.join(raw[i+24:i+24+2048] for i in range(0,len(raw),2352))[:left]
    out.write(data);h.update(data);left-=len(data)
  assert h.hexdigest()==meta['sha256'],name
  print(name,meta['size'],flush=True)
