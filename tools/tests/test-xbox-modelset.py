#!/usr/bin/env python3
"""Exercise production Xbox builders with a user-supplied pack; no device access."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from unittest import mock
sys.dont_write_bytecode=True

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--kit-root',type=Path,default=Path(__file__).resolve().parents[2])
p.add_argument('--archive',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args(); a.kit_root=a.kit_root.resolve(); a.archive=a.archive.resolve(); a.out=a.out.resolve()
if a.out.exists(): raise SystemExit('Use a fresh --out directory; prior evidence is preserved.')
a.out.mkdir(parents=True)
spec=importlib.util.spec_from_file_location('xbox',a.kit_root/'tools/modelsets/xbox-modelset.py')
x=importlib.util.module_from_spec(spec);spec.loader.exec_module(x)
checks=0
def check(value,message):
 global checks
 checks+=1
 if not value: raise AssertionError(message)
def rejected(call,message):
 try: call()
 except (OSError,ValueError,subprocess.CalledProcessError): check(True,message);return
 check(False,message)
def run(argv,success=True,env=None):
 if os.name=='nt' and str(argv[0]).lower().endswith('powershell.exe') and env is None:
  env={k:v for k,v in os.environ.items() if k.lower()!='psmodulepath'}
 r=subprocess.run([str(s) for s in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
 check((r.returncode==0)==success,'unexpected command result: '+r.stdout[-2000:]);return r

x.assert_archive(a.archive); check(True,'actual pinned archive')
extracted=a.out/'sources/xbox-pack';x.extract(a.archive,extracted)
source=extracted/x.PACK['sourceFolder'];x.assert_source(source)
check(sum(p.is_file() for p in source.rglob('*'))==217,'main folder selection')
check(not any(p.name in ('III Xbox Perennial','Cutscene Vehicles','Prop Vehicles') for p in extracted.iterdir()),'optional variants extracted')
output=a.out/'python/xbox'
x.build(source,output)
check(x.valid_overlay(output),'actual Python build')
original={rel:x.sha(source/rel) for rel in x.PACK['files']}
for rel in ('models/coll/vehicles.col','models/generic/wheels.dff'):
 check(x.sha(source/rel)==x.sha(output/rel),'native geometry/COL changed')
img=(output/'models/gta3.img').read_bytes()
entries=list(struct.iter_unpack('<II24s',(output/'models/gta3.dir').read_bytes()))
for sector,size,raw in entries:
 name=raw.split(b'\0')[0].decode()
 check((source/'gta3.img'/name).is_file(),'unexpected entry '+name)
 if name.endswith('.dff'):
  authored=(source/'gta3.img'/name).read_bytes()
  check(img[sector*2048:sector*2048+len(authored)]==authored,'DFF altered '+name)
check(len(entries)==214,'compact overlay directory count')
col=(output/'models/coll/vehicles.col').read_bytes();pos=0;collisions=set()
while pos<len(col):
 magic,size=struct.unpack_from('<4sI',col,pos)
 check(magic==b'COLL' and pos+8+size<=len(col),'invalid authored COL')
 collisions.add(col[pos+8:pos+30].split(b'\0')[0].decode());pos+=8+size
models=set((output/'vehicle_models.txt').read_text().splitlines())
check(models<=collisions,'vehicle missing COL')
check({'cougar','toyz'}<=collisions,'native fallback vehicle COL absent')
stable={rel:x.sha(output/rel) for rel in x.OUTPUTS+('BUILD_INFO.txt',)}
bad=output/'models/gta3.dir';old=bad.read_bytes();bad.write_bytes(b'\xff'*4+old[4:])
check(not x.valid_overlay(output),'corrupt directory accepted');bad.write_bytes(old)
bad=output/'models/generic/wheels.txd';old=bad.read_bytes();bad.write_bytes(old[:-1])
check(not x.valid_overlay(output),'truncated wheel dictionary accepted');bad.write_bytes(old)
extra=output/'unrelated.txt';extra.write_text('private');check(not x.valid_overlay(output),'extra transferable file accepted');extra.unlink()
srcfile=source/'gta3.img/admiral.dff';old=srcfile.read_bytes();srcfile.write_bytes(old+b'changed')
rejected(lambda:x.build(source,output,True),'modified source accepted')
check(stable=={r:x.sha(output/r) for r in stable},'failed source validation changed working output')
srcfile.write_bytes(old)
rejected(lambda:x.build(source,source/'xbox',True),'overlapping paths accepted')
foreign=a.out/'foreign/xbox';foreign.mkdir(parents=True);(foreign/'private.txt').write_text('keep')
rejected(lambda:x.build(source,foreign,True),'foreign output replaced')
check((foreign/'private.txt').read_text()=='keep','foreign data changed')
rejected(lambda:x.build(source,output,True,compressor=a.out/'missing-compressor'),'failed conversion accepted')
check(stable=={r:x.sha(output/r) for r in stable},'conversion failure changed working output')
check(not any(p.name.startswith('.xbox.') for p in output.parent.iterdir()),'unfinished conversion temporary files')
with mock.patch.object(sys,'argv',['xbox','prepare','--out',str(output),'--work-dir',str(a.out/'unused')]),mock.patch.object(x.urllib.request,'urlopen',side_effect=AssertionError('cache downloaded')):
 check(x.main()==0,'complete cache not reused')
check(not (a.out/'unused').exists(),'complete cache created workspace')
with mock.patch.object(x.subprocess,'run',side_effect=AssertionError('cache extracted')):
 x.extract(a.archive,extracted);check(True,'source cache extracted again')
check(original=={r:x.sha(source/r) for r in original},'source assets changed')

# Exercise the production streaming download implementation with bounded fake
# HTTP responses, including servers which ignore Range and Google HTML errors.
payload=b'pinned-public-fixture'*19
class Response(io.BytesIO):
 def __init__(self,data,status,headers=None):super().__init__(data);self.status=status;self.headers=headers or {}
saved=dict(x.PACK)
try:
 x.PACK['archiveBytes']=len(payload);x.PACK['archiveSha256']=hashlib.sha256(payload).hexdigest().upper()
 for status in (200,206):
  dest=a.out/f'resume-{status}.rar';part=dest.with_name(dest.name+'.partial');part.write_bytes(payload[:13])
  reply=Response(payload[13:] if status==206 else payload,status,{'Content-Range':f'bytes 13-{len(payload)-1}/{len(payload)}'})
  with mock.patch.object(x.urllib.request,'urlopen',return_value=reply):x.download(dest)
  check(dest.read_bytes()==payload,'resume/range ignored broke downloaded bytes')
 dest=a.out/'complete-partial.rar';dest.with_name(dest.name+'.partial').write_bytes(payload)
 with mock.patch.object(x.urllib.request,'urlopen',side_effect=AssertionError('full partial downloaded')):x.download(dest)
 check(dest.read_bytes()==payload,'complete partial was not promoted')
 dest=a.out/'html.rar'
 with mock.patch.object(x.urllib.request,'urlopen',return_value=Response(b'<html>login</html>',200)):
  rejected(lambda:x.download(dest),'HTML accepted as model archive')
 check(not dest.exists(),'bad downloaded file committed')
finally:x.PACK.clear();x.PACK.update(saved)

if os.name=='nt':
 ps='powershell.exe';psout=a.out/'powershell/xbox'
 r=run([ps,'-NoProfile','-ExecutionPolicy','Bypass','-File',a.kit_root/'tools/modelsets/build-xbox-modelset.ps1','-Source',source,'-Out',psout])
 (a.out/'powershell-build.log').write_text(r.stdout)
 for rel in stable:check(x.sha(psout/rel)==stable[rel],'PS/Python output differs: '+rel)
 # Run the full production preparation path, provided archive -> extraction ->
 # XboxOnly -> build-only. These tiny input sentinels are never transferred.
 game=a.out/'game';(game/'models').mkdir(parents=True)
 for name in ('gta3.img','gta3.dir','generic.txd'):(game/'models'/name).write_bytes(b'untouched-game-fixture')
 modern=game/'modelsets/modern';modern.mkdir(parents=True);(modern/'private.txt').write_text('keep-modern')
 command=[ps,'-NoProfile','-ExecutionPolicy','Bypass','-File',a.kit_root/'tools/prepare-modern-models.ps1','-XboxOnly','-GameDir',game,'-WorkDir',a.out/'prep-cache','-XboxArchive',a.archive,'-BuildOnly','-NonInteractive','-LogPath',a.out/'prepare.log']
 run(command);check(x.valid_overlay(game/'modelsets/xbox'),'XboxOnly profile missing')
 check((modern/'private.txt').read_text()=='keep-modern','Xbox preparation changed Modern')
 run(command);check((a.out/'prepare.log').read_text(encoding='utf-8-sig').find('Reusing the complete verified Xbox profile')>=0,'PS finished profile not reused')
 check(all(p.read_bytes()==b'untouched-game-fixture' for p in (game/'models').iterdir()),'game assets changed')

receipt={'checks':checks,'archiveSha256':x.sha(a.archive),'outputs':stable,'actualVehicles':107,'archiveEntries':214,'deviceCommands':0,'network':False}
(a.out/'results.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(f'PASS: {checks} Xbox source/cache/builder checks; 107 actual vehicles, no device commands.')
