"""Production shell updater with local Git remotes and mocked device calls.

No network, Android toolchain, APK install, or game launch is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kit', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bash', default='bash')
    ap.add_argument('--git', default='git')
    a = ap.parse_args()
    kit, out = a.kit.resolve(), a.out.resolve()
    if out.exists():
        raise RuntimeError('Choose a fresh test directory.')
    out.mkdir(parents=True)
    git = shutil.which(a.git) or a.git
    bash = shutil.which(a.bash) or a.bash
    results = []
    checks = 0

    def check(value, reason):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError(reason)

    def run(*args, **kw):
        r = subprocess.run([str(x) for x in args], capture_output=True, text=True, encoding='utf-8', errors='replace', **kw)
        if r.returncode:
            raise RuntimeError('Fixture command failed: ' + str(args) + '\n' + r.stdout + r.stderr)
        return r.stdout.strip()

    def g(root, *args):
        return run(git, '-C', root, *args)

    def config(root):
        g(root, 'config', 'user.email', 'updater-test@example.invalid')
        g(root, 'config', 'user.name', 'Updater fixture')
        g(root, 'config', 'core.autocrlf', 'false')

    def write(root, name, content):
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding='utf-8', newline='\n')

    def commit(root, version, extra=None):
        write(root, 'overlay/android/app/build.gradle.kts', f'versionCode = {version}\n')
        for name, data in (extra or {}).items():
            write(root, name, data)
        g(root, 'add', '.')
        g(root, 'commit', '-m', 'fixture ' + str(version))
        return g(root, 'rev-parse', 'HEAD')

    def setup(name, version=520):
        case = out / name
        case.mkdir()
        (case / "work").mkdir()
        remote, client = case / 'remote', case / 'client'
        run(git, 'init', '-b', 'master', remote)
        config(remote)
        for rel in ['UPDATE.sh', 'tools/update-and-install.sh']:
            write(remote, rel, (kit / rel).read_text())
        write(remote, 'tools/build-and-install.sh', '#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$TEST_BUILD_ARGS"\n')
        write(remote, 'README.md', 'fixture\n')
        commit(remote, version)
        run(git, '-c', 'core.autocrlf=false', 'clone', remote, client)
        config(client)
        return case, remote, client

    mockbin = out / 'mockbin'
    mockbin.mkdir()
    write(mockbin, 'git', '''#!/usr/bin/env bash
set -euo pipefail
if [[ "$*" == *'remote get-url origin'* ]]; then
  printf '%s\\n' 'https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git'
  exit 0
fi
args=()
for arg in "$@"; do
  if [ "$arg" = 'https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git' ]; then
    args+=("$TEST_LOCAL_REMOTE")
    printf '%s\\n' 'local-remote-rewrite' >> "$TEST_NETWORK_LOG"
  else args+=("$arg"); fi
done
exec "$TEST_REAL_GIT" -c core.autocrlf=false "${args[@]}"
''')
    (mockbin / 'git').chmod(0o755)

    def update(case, remote, client, success, *extra, expect_built=None):
        env = os.environ.copy()
        env.update(TEST_LOCAL_REMOTE=remote.as_posix(), TEST_REAL_GIT=Path(git).as_posix(),
                   TEST_NETWORK_LOG=(case / 'network.log').as_posix(),
                   TEST_BUILD_ARGS=(case / 'build-args.txt').as_posix())
        # Bash adds its own Git directories ahead of Windows PATH on startup.
        # Export the mock path inside bash so every official URL is rewritten.
        script = 'export PATH="/usr/bin:/bin:$PATH"; p="$1"; command -v cygpath >/dev/null && p="$(cygpath -u "$p")"; export PATH="$p:$PATH"; shift; exec bash "$@"'
        args = [bash, '-c', script, 'fixture', mockbin.as_posix(),
                (client / 'UPDATE.sh').as_posix(), '--work-dir', (case / 'work').as_posix(),
                '--log-path', (case / 'update.log').as_posix(), '--build-only', *extra]
        r = subprocess.run(args, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        (case / 'result.log').write_text(r.stdout + r.stderr)
        check((r.returncode == 0) == success, case.name + ': ' + r.stdout + r.stderr)
        built = (case / 'build-args.txt').exists()
        check(built == ((success and '--dry-run' not in extra) if expect_built is None else expect_built), case.name + ': unexpected build dispatch')
        if built:
            lines = (case / 'build-args.txt').read_text().splitlines()
            check('--update-only' in lines and '--build-only' in lines, 'APK-only options not forwarded')
            check('--work-dir' in lines and lines[lines.index('--work-dir') + 1] == (case / 'work').as_posix(), 'cache path changed')
        results.append(dict(case=case.name, exitCode=r.returncode, built=built))
        return r

    case, remote, client = setup('clean-forward')
    before = g(client, 'rev-parse', 'HEAD')
    newest = commit(remote, 521)
    update(case, remote, client, True, '--release')
    check('--release' in (case / 'build-args.txt').read_text().splitlines(), 'release option lost during forwarding')
    check(g(client, 'rev-parse', 'HEAD') == newest != before, 'clean source not fast-forwarded')

    case, remote, client = setup('dirty')
    write(client, 'README.md', 'preserve local edit\n')
    update(case, remote, client, False)
    check((client / 'README.md').read_text() == 'preserve local edit\n', 'tracked work changed')
    check(not (case / 'network.log').exists(), 'dirty source fetched')

    case, remote, client = setup('diverged')
    original = commit(client, 520, {'local.txt': 'local commit\n'})
    commit(remote, 521, {'remote.txt': 'remote commit\n'})
    update(case, remote, client, False)
    check(g(client, 'rev-parse', 'HEAD') == original, 'diverged HEAD changed')

    case, remote, client = setup('older-than-release-floor', 519)
    original = g(client, 'rev-parse', 'HEAD')
    update(case, remote, client, False)
    check(g(client, 'rev-parse', 'HEAD') == original, 'old remote modified HEAD')

    case, remote, client = setup('older-than-local')
    commit(client, 525)
    commit(remote, 521)
    update(case, remote, client, False)
    check('minimum 525' in (case / 'result.log').read_text(), 'local version floor lost')

    case, remote, client = setup('required-assets-marker')
    original = g(client, 'rev-parse', 'HEAD')
    commit(remote, 521, {'tools/update-required-assets.txt': 'Use normal installer\n'})
    update(case, remote, client, False)
    check(g(client, 'rev-parse', 'HEAD') == original, 'asset-required source merged')

    case, remote, client = setup('ignored-file-preserved')
    g(client, 'config', 'merge.autostash', 'true')
    write(client, '.git/info/exclude', 'collision.txt\n')
    write(client, 'collision.txt', 'private ignored file\n')
    commit(remote, 521, {'collision.txt': 'remote tracked file\n'})
    update(case, remote, client, False)
    check((client / 'collision.txt').read_text() == 'private ignored file\n', 'ignored file overwritten')
    check(not g(client, 'stash', 'list'), 'updater created a stash')

    case, remote, client = setup('zip-managed-cache')
    zipkit = case / 'zip-kit'
    shutil.copytree(client, zipkit, ignore=shutil.ignore_patterns('.git'))
    before = {p.relative_to(zipkit).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in zipkit.rglob('*') if p.is_file()}
    newest = commit(remote, 521)
    update(case, remote, zipkit, True)
    cache = case / 'work/source-kit'
    check(g(cache, 'rev-parse', 'HEAD') == newest, 'ZIP managed clone not updated')
    check(before == {p.relative_to(zipkit).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in zipkit.rglob('*') if p.is_file()}, 'ZIP source changed')

    case, remote, client = setup('zip-newer-cache')
    zipkit = case / 'zip-kit'
    shutil.copytree(client, zipkit, ignore=shutil.ignore_patterns('.git'))
    cache = case / 'work/source-kit'
    run(git, '-c', 'core.autocrlf=false', 'clone', remote, cache)
    config(cache)
    commit(cache, 525)
    commit(remote, 521)
    update(case, remote, zipkit, False)
    check('minimum 525' in (case / 'result.log').read_text(), 'cached version floor lost')

    case, remote, client = setup('dry-run-no-network')
    update(case, remote, client, True, '--dry-run')
    check(not (case / 'network.log').exists(), 'dry run fetched or cloned')

    case, remote, client = setup('builder-error-propagated')
    commit(remote, 521, {'tools/build-and-install.sh': '#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$TEST_BUILD_ARGS"\nexit 37\n'})
    r = update(case, remote, client, False, expect_built=True)
    check(r.returncode == 37, 'builder failure exit was swallowed')

    # Exercise the actual install phase and early exit, with no Android work.
    source = (kit / 'tools/build-and-install.sh').read_text()
    start = source.index('require_installed_for_update() {')
    end = source.index('\n# ---- main', start)
    installed_function = source[start:end]
    install_phase = source[source.index('step 7 "Installing'):source.index('# Run the save-provider bootstrap query')]
    for name, existing, fail_install in [('install-apk-only', True, False), ('no-existing-install', False, False), ('signature-failure-preserved', True, True)]:
        case = out / name
        case.mkdir()
        adb = case / 'adb'
        write(case, 'adb', '''#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$TEST_ADB_LOG"
case "$*" in
 *'shell pm path'*) [ "$TEST_EXISTING" = 1 ] && echo 'package:/data/app/mock/base.apk'; exit 0 ;;
 *'install -r'*) [ "$TEST_INSTALL_FAIL" = 1 ] && { echo 'INSTALL_FAILED_UPDATE_INCOMPATIBLE'; exit 1; }; exit 0 ;;
 *) echo 'forbidden device operation'; exit 99 ;;
esac
''')
        adb.chmod(0o755)
        write(case, 'phase.sh', '''#!/usr/bin/env bash
set -uo pipefail
die(){ echo "$*"; exit 1; }
step(){ :; }
select_quest_device(){ :; }
UPDATE_ONLY=1; SERIAL=mock; PACKAGE_NAME=com.miamivr.quest; LOG_PATH=fixture
APK=mock.apk; ADB="$TEST_ADB"
''' + installed_function + '\n' + install_phase + '\necho forbidden-fallthrough; exit 99\n')
        env = os.environ.copy()
        env.update(TEST_ADB=adb.as_posix(), TEST_ADB_LOG=(case / 'adb.log').as_posix(),
                   TEST_EXISTING=str(int(existing)), TEST_INSTALL_FAIL=str(int(fail_install)))
        r = subprocess.run([bash, '-c', 'export PATH="/usr/bin:/bin:$PATH"; exec bash "$1"', 'fixture', (case / 'phase.sh').as_posix()], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        (case / 'result.log').write_text(r.stdout + r.stderr)
        check((r.returncode == 0) == (existing and not fail_install), name + ': incorrect outcome')
        calls = (case / 'adb.log').read_text()
        check(('install -r' in calls) == existing, 'missing existing-app guard')
        check(not any(x in calls for x in ['uninstall', 'content query', ' push ', 'am start', 'force-stop', ' -d ']), 'forbidden device mutation')
        results.append(dict(case=name, exitCode=r.returncode))

    discovery = source[source.index('step 1 "Preparing'):source.index('step 2 "Preparing')]
    for failed in ['git', 'java', 'sdk']:
        case = out / ('discovery-failed-' + failed)
        case.mkdir()
        write(case, 'phase.sh', '''#!/usr/bin/env bash
set -uo pipefail
die(){ echo "$*"; exit 1; }
step(){ :; }
find_git(){ echo git >> "$TEST_TOOL_LOG"; [ "$TEST_FAIL_TOOL" != git ] || return 7; echo /mock/git; }
find_java_home(){ echo java >> "$TEST_TOOL_LOG"; [ "$TEST_FAIL_TOOL" != java ] || return 9; echo /mock/jdk; }
find_android_sdk(){ echo sdk >> "$TEST_TOOL_LOG"; [ "$TEST_FAIL_TOOL" != sdk ] || return 11; echo /mock/sdk; }
ensure_sdk_packages(){ echo forbidden-continued >> "$TEST_TOOL_LOG"; }
UPDATE_ONLY=1; BUILD_ONLY=1
''' + discovery + '\necho forbidden-fallthrough; exit 99\n')
        env = os.environ.copy()
        env.update(TEST_FAIL_TOOL=failed, TEST_TOOL_LOG=(case / 'tools.log').as_posix())
        r = subprocess.run([bash, (case / 'phase.sh').as_posix()], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        (case / 'result.log').write_text(r.stdout + r.stderr)
        calls = (case / 'tools.log').read_text().splitlines()
        check(r.returncode == 1, failed + ': tool failure was swallowed')
        check(calls == ['git', 'java', 'sdk'][:['git', 'java', 'sdk'].index(failed) + 1], failed + ': discovery continued after failure')
        results.append(dict(case='discovery-failed-' + failed, exitCode=r.returncode))

    build_phase = source[source.index('BUILD_VARIANT="debug"'):source.index('if [ "$BUILD_ONLY" -eq 1 ]; then', source.index('BUILD_VARIANT="debug"'))]
    for release in [0, 1]:
        case = out / ('build-variant-' + str(release))
        case.mkdir()
        variant = 'release' if release else 'debug'
        write(case, 'android/app/build/outputs/apk/' + variant + '/app-' + variant + '.apk', 'mock APK')
        write(case, 'gradle', '#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$TEST_GRADLE_ARGS"\n')
        (case / 'gradle').chmod(0o755)
        write(case, 'phase.sh', '#!/usr/bin/env bash\nset -uo pipefail\nstep(){ :; }\ndie(){ echo "$*"; exit 1; }\n' +
              'ASSEMBLED_DIR="$TEST_BUILD_ROOT"; GRADLE_BIN="$TEST_GRADLE"; RELEASE_BUILD=' + str(release) + '\n' + build_phase)
        env = os.environ.copy()
        env.update(TEST_BUILD_ROOT=case.as_posix(), TEST_GRADLE=(case / 'gradle').as_posix(), TEST_GRADLE_ARGS=(case / 'gradle-args.txt').as_posix())
        r = subprocess.run([bash, '-c', 'export PATH="/usr/bin:/bin:$PATH"; exec bash "$1"', 'fixture', (case / 'phase.sh').as_posix()], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        (case / 'result.log').write_text(r.stdout + r.stderr)
        check(r.returncode == 0, 'APK build variant phase failed: ' + r.stdout + r.stderr)
        args = (case / 'gradle-args.txt').read_text().splitlines()
        check(args[0] == (':app:assembleRelease' if release else ':app:assembleDebug'), 'wrong Gradle task')
        check('-PmiamivrDevTools=false' in args and ('app-' + variant + '.apk') in r.stdout, 'wrong APK path or developer tools enabled')
        results.append(dict(case='build-variant-' + variant, exitCode=r.returncode))

    def shell_function(name):
        start = source.index(name + '() {')
        return source[start:source.index('\n}\n', start) + 3]
    for license_fail in [0, 1]:
        case = out / ('sdk-license-' + str(license_fail))
        case.mkdir()
        write(case, 'sdkmanager', '''#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$TEST_SDK_CALLS"
if [[ "$*" == *--licenses* ]]; then
  read -r answer
  exit "$TEST_LICENSE_FAIL"
fi
printf 'installed' > "$TEST_SDK_INSTALLED"
''')
        (case / 'sdkmanager').chmod(0o755)
        write(case, 'phase.sh', '''#!/usr/bin/env bash
set -uo pipefail
die(){ echo "$*"; exit 1; }
get_missing_sdk_packages(){ [ -f "$TEST_SDK_INSTALLED" ] || echo 'platform-tools'; }
ensure_android_command_line_tools(){ echo "$TEST_SDK_MANAGER"; }
NON_INTERACTIVE=0
''' + shell_function('checked') + '\n' + shell_function('ensure_sdk_packages') + '\nensure_sdk_packages /mock/sdk\n')
        env = os.environ.copy()
        env.update(TEST_LICENSE_FAIL=str(license_fail), TEST_SDK_CALLS=(case / 'calls.log').as_posix(), TEST_SDK_INSTALLED=(case / 'installed').as_posix(), TEST_SDK_MANAGER=(case / 'sdkmanager').as_posix())
        r = subprocess.run([bash, '-c', 'export PATH="/usr/bin:/bin:$PATH"; exec bash "$1"', 'fixture', (case / 'phase.sh').as_posix()], env=env, input='y\n', capture_output=True, text=True, encoding='utf-8', errors='replace')
        (case / 'result.log').write_text(r.stdout + r.stderr)
        check((r.returncode == 0) == (license_fail == 0), 'license failure/SIGPIPE status mishandled')
        calls = (case / 'calls.log').read_text().splitlines()
        check(len(calls) == (1 if license_fail else 2), 'SDK component install continued after failed license command')
        check((case / 'installed').exists() == (license_fail == 0), 'unexpected SDK component write')
        results.append(dict(case='sdk-license-' + str(license_fail), exitCode=r.returncode))

    for rel in ['UPDATE.sh', 'tools/update-and-install.sh', 'tools/build-and-install.sh']:
        run(bash, '-n', kit / rel)
        checks += 1
    report = dict(checks=checks, cases=results, production={rel: hashlib.sha256((kit / rel).read_bytes()).hexdigest().upper() for rel in ['UPDATE.sh', 'tools/update-and-install.sh', 'tools/build-and-install.sh']}, network=False, device=False)
    (out / 'results.json').write_text(json.dumps(report, indent=2))
    print(f'PASS: {checks} production Unix updater checks across {len(results)} cases; local remotes only, mocked adb, no network/device work.')


if __name__ == '__main__':
    main()
