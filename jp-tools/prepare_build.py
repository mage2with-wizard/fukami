import pathlib,re,json,hashlib,shutil
P=pathlib.Path('/tmp/fukami-region-analysis');R=pathlib.Path('/tmp/fukami-jp-source');E=R/'jp-experiment';E.mkdir(exist_ok=True)
m={int(a,16):int(b,16) for a,b in json.loads((P/'address-map.json').read_text()).items()}
def trans(s):
 def sub(t):
  if t[1]:
   a=int(t[1],16);v=m.get(a,a);return 'sub_%08X_0x%x'%(v,v)
  a=int(t[2],16);return '0x'+format(m[a],'x') if a in m else t[0]
 return re.sub(r'sub_([0-9A-Fa-f]{8})_0x[0-9a-fA-F]+|0x([0-9a-fA-F]+)',sub,s)

s=(R/'src/product/patches.cpp').read_text();a=s.index('// RR5 native hot functions');b=s.index('// 0x298eb0 job-table wait',a)
s=s[:a]+'namespace rrv_native { void registerAll(PS2Runtime&) {} }\n'+s[b:]
s=trans(s)
# Add declarations for any generated forwarders which live in the stubs header.
s=s.replace('#include "ps2_recompiled_functions.h"','#include "ps2_recompiled_functions.h"\n#include "ps2_recompiled_stubs.h"')
(E/'patches_jp.cpp').write_text(s)
# Existing hand-maintained stub routes preserved, relocated to JP addresses.
(E/'stubs_jp.cpp').write_text(trans((R/'src/rrv_stubs.cpp').read_text()))
# Headers with guest memory references get relocated copies in experimental include directory.
for f in (R/'src/product').glob('*.h'):
 (E/f.name).write_text(trans(f.read_text()))
# Main stays the upstream implementation, with experimental settings path only.
s=(R/'src/product/main_product.cpp').read_text().replace('fukami::app::defaultIniPath()', 'std::filesystem::path("/tmp/fukami-jp-source/jp-experiment/test.ini")')
(E/'main_jp.cpp').write_text(s)
# Build a truthful typed inventory for a private copy of the installed GS package.
B=R/'jp-build';dest=B/'share/game/.rrv-resource-packages/current';src=pathlib.Path.home()/'Applications/Fukami/share/game/.rrv-resource-packages/current'
shutil.copytree(src,dest,dirs_exist_ok=True);(B/'share/game/.rrv-resource-package.lock').touch()
identity=(dest/'identity').read_text().splitlines();rows=[]
for f in sorted(dest.rglob('*')):
 if f.name=='identity':continue
 n=f.relative_to(dest).as_posix();d=f.read_bytes() if f.is_file() else b''
 rows.append('{"%s", EntryType::%s, %d, "%s"}'%(n,'file' if f.is_file() else 'directory',len(d),hashlib.sha256(d).hexdigest() if f.is_file() else ''))
(E/'binding.cpp').write_text('#include "rrv_resource_package.h"\nnamespace rrv::resource_package {\nstatic const InventoryEntry entries[] = {\n'+',\n'.join(rows)+'\n};\nconst Binding rrv_resource_package_binding{"'+identity[0]+'","'+identity[1]+'","librrv-pcsx2-gs-bridge.so",entries,sizeof(entries)/sizeof(entries[0])};\n}\n')
# Hash implementation only, no CD extractor UI needed in this standalone diagnostic.
s=(R/'src/app/disc_extract.cpp').read_text()
print('prepared',len(rows),'GS resource entries')
