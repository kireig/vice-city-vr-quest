#!/usr/bin/env python3
"""Build/validate the optional Xbox vehicle profile without modifying inputs."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import urllib.request
import uuid

TOOLS = Path(__file__).resolve().parent
PACK = json.loads((TOOLS / 'xbox-pack.json').read_text(encoding='utf-8'))
OUTPUTS = ('models/gta3.img', 'models/gta3.dir', 'models/coll/vehicles.col',
           'models/generic/wheels.dff', 'models/generic/wheels.txd', 'vehicle_models.txt')


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest().upper()


def assert_archive(path):
    path = Path(path)
    if path.stat().st_size != PACK['archiveBytes'] or sha(path) != PACK['archiveSha256']:
        raise ValueError(f'Xbox archive failed pinned size/SHA256 verification: {path}')


def assert_source(root):
    root = Path(root)
    entries = list(root.rglob('*'))
    if any(p.is_symlink() for p in entries) or sum(p.is_file() for p in entries) != len(PACK['files']):
        raise ValueError('Unexpected files or links in the extracted Xbox main pack.')
    for relative, expected in PACK['files'].items():
        if sha(root / relative) != expected:
            raise ValueError(f'Extracted Xbox source failed SHA256 verification: {relative}')


def assert_img(root):
    root = Path(root)
    data = (root / 'models/gta3.dir').read_bytes()
    length = (root / 'models/gta3.img').stat().st_size
    if len(data) != 214 * 32:
        raise ValueError('Xbox archive must contain exactly 107 DFF/TXD pairs.')
    names = set()
    expected_sector = 0
    for sector, size, raw_name in struct.iter_unpack('<II24s', data):
        name = raw_name.split(b'\0')[0].decode('ascii')
        if not re.fullmatch(r'[a-z0-9_]{1,19}\.(dff|txd)', name) or name in names or \
                size < 1 or sector != expected_sector or (sector + size) * 2048 > length:
            raise ValueError(f'Invalid Xbox archive directory entry: {name}')
        expected_sector = sector + size
        names.add(name)
    if expected_sector * 2048 != length:
        raise ValueError('Xbox IMG has an invalid trailing size.')
    models = (root / 'vehicle_models.txt').read_text().splitlines()
    if len(models) != 107 or len(set(models)) != 107 or \
            any(name + ext not in names for name in models for ext in ('.dff', '.txd')):
        raise ValueError('Invalid Xbox vehicle manifest.')


def valid_overlay(root):
    root = Path(root)
    try:
        lines = set((root / 'BUILD_INFO.txt').read_text().splitlines())
        required = {'Profile=XBOX', 'BuilderVersion=' + PACK['builderVersion'],
                    'SourceArchiveSHA256=' + PACK['archiveSha256']}
        if not required <= lines:
            return False
        entries = list(root.rglob('*'))
        if any(p.is_symlink() for p in entries) or sum(p.is_file() for p in entries) != 7:
            return False
        for rel in OUTPUTS:
            if not (root / rel).stat().st_size or f'SHA256 {rel}={sha(root / rel)}' not in lines:
                return False
        assert_img(root)
        return True
    except (OSError, ValueError, UnicodeError):
        return False


def remove_temporary(path, parent):
    path, parent = Path(path).absolute(), Path(parent).resolve()
    if path.parent.resolve() != parent or not re.fullmatch(r'\.xbox\.(build|source|previous)-[0-9a-f]+', path.name):
        raise ValueError(f'Refusing an unexpected temporary directory: {path}')
    if path.is_symlink():
        raise ValueError('Refusing to remove a linked temporary directory.')
    if path.exists():
        shutil.rmtree(path)


def replace_directory(fresh, output, backup):
    had_output = output.exists()
    if had_output:
        output.rename(backup)
    try:
        fresh.rename(output)
    except OSError:
        if had_output and not output.exists():
            backup.rename(output)
        raise
    remove_temporary(backup, output.parent)


def build(source, output, force=False, compressor=None):
    source, output = Path(source).resolve(), Path(output).absolute()
    if (source / PACK['sourceFolder']).is_dir():
        source /= PACK['sourceFolder']
    assert_source(source)
    if output.name != 'xbox' or source == output or source in output.parents or output in source.parents:
        raise ValueError('Use a separate output folder named xbox, outside the input source.')
    if output.exists():
        if not force or 'Profile=XBOX' not in (output / 'BUILD_INFO.txt').read_text().splitlines():
            raise ValueError('Refusing to replace an existing folder without -Force and a generated Xbox marker.')
    output.parent.mkdir(parents=True, exist_ok=True)
    unique = uuid.uuid4().hex
    stage = output.parent / ('.xbox.build-' + unique)
    work = output.parent / ('.xbox.source-' + unique)
    backup = output.parent / ('.xbox.previous-' + unique)
    stage.mkdir()
    work.mkdir()
    try:
        for folder in ('models/coll', 'models/generic'):
            (stage / folder).mkdir(parents=True)
        for file in sorted((source / 'gta3.img').iterdir()):
            shutil.copyfile(file, work / file.name.lower())
        for rel in OUTPUTS[2:5]:
            shutil.copyfile(source / rel, stage / rel)
        if compressor:
            compressor = str(Path(compressor).resolve())
        else:
            bundled = TOOLS / ('txdcompress.exe' if os.name == 'nt' else 'txdcompress')
            if bundled.is_file():
                compressor = str(bundled)
            else:
                compiler = shutil.which('c++') or shutil.which('g++')
                if not compiler:
                    raise ValueError('Install a C++ compiler to build the included texture compressor (for example g++).')
                compressor = str(stage / 'txdcompress')
                subprocess.run([compiler, '-O2', '-o', compressor, str(TOOLS / 'txdcompress.cpp')], check=True)
        for txd in sorted(work.glob('*.txd')) + [stage / 'models/generic/wheels.txd']:
            subprocess.run([compressor, str(txd)], check=True)
        if (stage / 'txdcompress').exists():
            (stage / 'txdcompress').unlink()
        with (stage / 'models/gta3.img').open('wb') as img, (stage / 'models/gta3.dir').open('wb') as directory:
            sector = 0
            for file in sorted(work.iterdir()):
                data = file.read_bytes()
                size = math.ceil(len(data) / 2048)
                directory.write(struct.pack('<II24s', sector, size, file.name.encode('ascii')))
                img.write(data)
                img.write(b'\0' * (size * 2048 - len(data)))
                sector += size
        models = sorted(file.stem for file in work.glob('*.dff'))
        (stage / 'vehicle_models.txt').write_bytes(('\n'.join(models) + '\n').encode())
        assert_img(stage)
        lines = ['Vice City VR Xbox vehicle profile', 'Profile=XBOX', 'BuilderVersion=' + PACK['builderVersion'],
                 'SourceArchiveSHA256=' + PACK['archiveSha256'], 'VehicleCount=107', 'ArchiveEntries=214']
        lines += [f'SHA256 {rel}={sha(stage / rel)}' for rel in OUTPUTS]
        lines += ['Assets are supplied by the player from the external pack; do not redistribute this generated folder.']
        (stage / 'BUILD_INFO.txt').write_bytes(('\n'.join(lines) + '\n').encode())
        if not valid_overlay(stage):
            raise ValueError('Completed Xbox profile failed validation.')
        replace_directory(stage, output, backup)
        print(f'Xbox profile built and verified: {output} (107 vehicles; Modern untouched).')
    finally:
        remove_temporary(stage, output.parent)
        remove_temporary(work, output.parent)


def download(destination):
    destination = Path(destination)
    try:
        assert_archive(destination)
        print(f'Reusing verified download: {destination}')
        return
    except (OSError, ValueError):
        pass
    partial = destination.with_name(destination.name + '.partial')
    offset = partial.stat().st_size if partial.exists() else 0
    if offset == PACK['archiveBytes']:
        if sha(partial) == PACK['archiveSha256']:
            partial.replace(destination)
            return
        partial.unlink()
        offset = 0
    if offset > PACK['archiveBytes']:
        partial.unlink()
        offset = 0
    request = urllib.request.Request(PACK['archiveUrl'], headers={'Range': f'bytes={offset}-'} if offset else {})
    with urllib.request.urlopen(request, timeout=60) as response:
        append = offset and response.status == 206
        if append and not response.headers.get('Content-Range', '').startswith(f'bytes {offset}-'):
            raise ValueError('Download returned an incorrect resume offset.')
        total = offset if append else 0
        with partial.open('ab' if append else 'wb') as target:
            for block in iter(lambda: response.read(1024 * 1024), b''):
                total += len(block)
                if total > PACK['archiveBytes']:
                    raise ValueError('Download exceeds the pinned archive size.')
                target.write(block)
    assert_archive(partial)
    partial.replace(destination)


def extract(archive, destination):
    assert_archive(archive)
    destination = Path(destination)
    try:
        assert_source(destination / PACK['sourceFolder'])
        print(f'Reusing verified extraction: {destination}')
        return
    except (OSError, ValueError):
        pass
    destination.parent.mkdir(parents=True, exist_ok=True)
    unique = uuid.uuid4().hex
    fresh = destination.parent / ('.xbox.source-' + unique)
    backup = destination.parent / ('.xbox.previous-' + unique)
    fresh.mkdir()
    try:
        command = None
        for name in ('bsdtar', 'tar'):
            tool = shutil.which(name)
            if tool and subprocess.run([tool, '-tf', str(archive)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                command = [tool, '-xf', str(archive), '-C', str(fresh), PACK['sourceFolder']]
                break
        if not command:
            for name in ('7zz', '7z'):
                tool = shutil.which(name)
                if tool:
                    command = [tool, 'x', '-y', '-o' + str(fresh), str(archive), PACK['sourceFolder'] + '/*']
                    break
        if not command and shutil.which('unrar'):
            command = [shutil.which('unrar'), 'x', '-o+', str(archive), PACK['sourceFolder'] + '/*', str(fresh) + '/']
        if not command:
            raise ValueError('Install bsdtar (libarchive-tools), 7-Zip, or unrar to extract the Xbox RAR archive.')
        subprocess.run(command, check=True)
        assert_source(fresh / PACK['sourceFolder'])
        replace_directory(fresh, destination, backup)
    finally:
        remove_temporary(fresh, destination.parent)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'prepare', 'validate'))
    parser.add_argument('--source')
    parser.add_argument('--out', required=True)
    parser.add_argument('--archive')
    parser.add_argument('--work-dir')
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--accept-downloads', action='store_true')
    parser.add_argument('--non-interactive', action='store_true')
    args = parser.parse_args()
    if args.command == 'validate':
        return 0 if valid_overlay(args.out) else 1
    if args.command == 'build':
        if not args.source:
            parser.error('--source is required to build')
        build(args.source, args.out, args.force)
        return 0
    if valid_overlay(args.out):
        print(f'Reusing the complete verified Xbox profile: {args.out}')
        return 0
    if not args.work_dir:
        parser.error('--work-dir is required to prepare')
    work = Path(args.work_dir).resolve()
    (work / 'downloads').mkdir(parents=True, exist_ok=True)
    archive = Path(args.archive).resolve() if args.archive else work / 'downloads' / PACK['archiveFile']
    if args.archive:
        assert_archive(archive)
    else:
        try:
            assert_archive(archive)
        except (OSError, ValueError):
            if not args.accept_downloads:
                if args.non_interactive:
                    raise ValueError('Pass --accept-downloads or a verified --archive in non-interactive mode.')
                if input('Download the optional 35 MB Fixed Xbox Vehicles 1.3 pack? [Y/n] ').lower() not in ('', 'y', 'yes'):
                    raise ValueError('Xbox download cancelled; no Quest data changed.')
            download(archive)
    source = work / 'sources/xbox-pack'
    extract(archive, source)
    build(source / PACK['sourceFolder'], args.out, True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
