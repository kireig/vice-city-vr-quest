#!/usr/bin/env python3
"""Run the full Unix profile installer with a local fake ADB function."""
import argparse,json,os
from pathlib import Path
import shutil,subprocess,sys
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--kit-root',type=Path,default=Path(__file__).resolve().parents[2])
p.add_argument('--profile-root',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--bash',default='bash')
a=p.parse_args();a.out=a.out.resolve();a.kit_root=a.kit_root.resolve();a.profile_root=a.profile_root.resolve()
if a.out.exists():raise SystemExit('Use a fresh output directory.')
a.out.mkdir(parents=True)
source=(a.kit_root/'tools/install-modern-models.sh').read_text()
source=source.replace('SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"','SCRIPT_DIR="$TEST_KIT/tools"')
marker='echo "Vice City VR - Modern model installer'
assert marker in source
adapters=r'''
find_adb() { echo fixture_adb; }
fixture_adb() {
  printf '%s\t' "$@" >> "$TEST_CALLS"; printf '\n' >> "$TEST_CALLS"
  if [ "${1:-}" = -s ]; then shift 2; fi
  if [ "$1" = devices ]; then printf 'List of devices attached\nfixture-quest device product:test\n'; return 0; fi
  if [ "$1 $2" = 'shell pm' ]; then echo 'package:/data/app/fixture.apk'; return 0; fi
  if [ "$1 $2" = 'shell stat' ] || [ "$1 $2" = 'shell sha256sum' ]; then
    local last="${@: -1}" relative
    relative="${last#*/.xbox-incoming-*/}"
    [ "$last" != "$relative" ] || return 91
    if [ "$2" = stat ]; then stat -c%s "$TEST_PROFILE/$relative"; return $?; fi
    if [ "$TEST_FAILURE" = hash ]; then printf '%064d  fixture\n' 0; else sha256sum "$TEST_PROFILE/$relative"; fi
    return 0
  fi
  if [ "$1 $2" = 'shell chmod' ] && [ "$TEST_FAILURE" = permissions ]; then return 1; fi
  if [ "$1 $2" = 'shell mv' ] && [[ "$3" = */.xbox-incoming-* ]] && [ "$TEST_FAILURE" = commit ]; then return 1; fi
  echo ok
}
'''
source=source.replace(marker,adapters+'\n'+marker,1)
script=a.out/'installer-production.sh';script.write_text(source,newline='\n')
binpath=a.out/'bin';binpath.mkdir()
(binpath/'python3').write_text('#!/usr/bin/env bash\nexec '+json.dumps(sys.executable.replace('\\','/'))+' "$@"\n',newline='\n');(binpath/'python3').chmod(0o755)
checks=0
def check(value,message):
 global checks
 checks+=1
 if not value:raise AssertionError(message)
for failure in ('none','hash','commit','permissions'):
 calls=a.out/(failure+'-calls.tsv')
 env=os.environ.copy();env.update(TEST_KIT=a.kit_root.as_posix(),TEST_PROFILE=a.profile_root.as_posix(),TEST_CALLS=calls.as_posix(),TEST_FAILURE=failure)
 # Git Bash supplies the usual POSIX tools; the shim selects the same Python
 # interpreter as the test, including on Windows hosts.
 env['PATH']=str(binpath)+os.pathsep+env['PATH']
 r=subprocess.run([a.bash,str(script),'--profile','xbox','--modern-dir',str(a.profile_root),'--non-interactive','--log-path',str(a.out/(failure+'-transcript.log'))],env=env,capture_output=True,text=True)
 (a.out/(failure+'-output.log')).write_text(r.stdout+r.stderr)
 check((r.returncode==0)==(failure=='none'),'wrong installer exit: '+r.stdout[-1200:]+r.stderr[-400:])
 events=[line.rstrip('\t').split('\t') for line in calls.read_text().splitlines()]
 strings=[' '.join(e) for e in events]
 check(not any(any(s in line for s in ('uninstall','shell am start','vr_settings','gta_vc.set','/modelsets/modern')) for line in strings),'other profile/data touched')
 check(sum('push' in e for e in events)==7,'not seven profile uploads')
 check(sum('sha256sum' in e for e in events)==(1 if failure=='hash' else 0 if failure=='permissions' else 7),'wrong hash count')
 check(all(e[:2]==['-s','fixture-quest'] for e in events if 'rm' in e),'cleanup not scoped to selected headset')
 grants=[e for e in events if e[2:6]==['shell','chmod','-R','a+rX']]
 check(len(grants)==1 and '/.xbox-incoming-' in grants[0][-1], 'missing staged app read/traverse access')
 moves=[e for e in events if 'mv' in e]
 if moves:check(events.index(grants[0])<events.index(moves[0]),'permissions granted after activation')
 if failure in ('hash','permissions'):check(not moves,'hash failure moved active profile')
 elif failure=='commit':
  check(len(moves)==3,'missing rollback');check('.xbox-backup-' in moves[-1][-2] and moves[-1][-1].endswith('/xbox'),'rollback destination')
 else:check(len(moves)==2,'missing staged atomic replacement')
(a.out/'results.json').write_text(json.dumps({'checks':checks,'deviceCommands':0,'cases':4},indent=2)+'\n')
print(f'PASS {checks} production Unix Xbox installer checks; mocked ADB only.')
