#!/usr/bin/env python3
"""Versioned build receipts and read-only launcher status. No ROMs are read.

Kept identical in snesrecomp/tools and the trilogy launcher's scripts so a
launcher can inspect older games without adding a framework dependency.
"""
import argparse
import configparser
import datetime
import hashlib
import os
from pathlib import Path
import subprocess

INPUTS = ['src', 'recomp', 'mods', 'assets', 'launcher_assets', 'CMakeLists.txt',
          'framework_pins.txt', 'rom_identity.txt', 'VERSION', 'tools', 'scripts', '.gitmodules']


def git(repo, *args, timeout=10):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True,
                          timeout=timeout, check=True).stdout


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source_digest(repo, revision, paths):
    # Hash files, not the shape of a git diff: committing a dirty build must
    # not turn identical source into a false rebuild warning. Ignored ROMs,
    # generated code, saves and build output never enter this list.
    h = hashlib.sha256()
    names = git(repo, 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', *paths)
    for raw in sorted(set(names.split(b'\0'))):
        if not raw:
            continue
        p = repo / os.fsdecode(raw)
        if p.is_symlink():
            content = b'link:' + os.fsencode(os.readlink(p))
        elif p.is_file():
            content = digest(p).encode()
        else:
            continue  # deleted files and gitlinks (modules are hashed separately)
        h.update(raw + b'\0' + content + b'\0')
    return h.hexdigest()


def modules(repo):
    found = []
    for line in git(repo, 'submodule', 'status', '--recursive').decode().splitlines():
        if line.startswith('-'):
            continue
        path = line[42:].split(' (', 1)[0]
        if path:
            found.append(path)
    return found


def write_ini(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f'.{os.getpid()}.tmp')
    with temporary.open('w', encoding='utf-8') as out:
        data.write(out)
    os.replace(temporary, path)


def receipt(repo, exe):
    data = configparser.ConfigParser(interpolation=None)
    data['Build'] = {'format': '1', 'revision': 'unknown', 'executable_sha256': digest(exe),
                     'built_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    try:
        data['Build']['revision'] = git(repo, 'rev-parse', 'HEAD').decode().strip()
        for relative in ['.'] + modules(repo):
            directory = repo / relative
            revision = git(directory, 'rev-parse', 'HEAD').decode().strip()
            paths = INPUTS if relative == '.' else ['.']
            data['Source:' + relative] = {'revision': revision, 'paths': '\n'.join(paths),
                                         'digest': source_digest(directory, revision, paths)}
    except (subprocess.SubprocessError, OSError):
        # Source exports/setup packs have no .git; the game can still be built.
        pass
    return data


def status(repo, exe, fetch):
    data = configparser.ConfigParser(interpolation=None)
    state = {'label': 'VERSION UNKNOWN', 'detail': 'Update and build once to record this game version.',
             'built_revision': 'unknown', 'source_revision': 'unknown', 'repo': str(repo),
             'checked_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    data['Status'] = state
    state = data['Status']
    if not exe.is_file():
        state.update(label='NOT BUILT', detail='Build this game first.')
        return data
    record = configparser.ConfigParser(interpolation=None)
    try:
        record.read(exe.parent / 'build-info.ini', encoding='utf-8')
        state['built_revision'] = record.get('Build', 'revision', fallback='unknown')
        state['built_at'] = record.get('Build', 'built_at', fallback='')
        state['source_revision'] = git(repo, 'rev-parse', 'HEAD').decode().strip()
        branch = git(repo, 'symbolic-ref', '--quiet', '--short', 'HEAD').decode().strip()
        remote = git(repo, 'config', '--get', f'branch.{branch}.remote').decode().strip()
        merge = git(repo, 'config', '--get', f'branch.{branch}.merge').decode().strip()
    except (subprocess.SubprocessError, OSError, configparser.Error):
        branch = remote = merge = ''
    update = False
    try:
        if remote and remote != '.' and merge.startswith('refs/heads/'):
            tracking = f'refs/remotes/{remote}/{merge[len("refs/heads/"):]}'
            if fetch:
                try:
                    git(repo, 'fetch', '--no-tags', '--no-write-fetch-head', remote,
                        f'{merge}:{tracking}', timeout=6)
                    state['network'] = 'checked'
                except (subprocess.SubprocessError, OSError):
                    state['network'] = 'offline; using cached update information'
            ahead = int(git(repo, 'rev-list', '--count', f'HEAD..{tracking}').decode())
            update = ahead > 0
    except (subprocess.SubprocessError, OSError, ValueError):
        pass
    try:
        valid = record.get('Build', 'format') == '1' and record.has_section('Source:.')
        if valid:
            changed = record.get('Build', 'executable_sha256') != digest(exe)
            expected = {'.'} | set(modules(repo))
            recorded = {section[7:] for section in record.sections() if section.startswith('Source:')}
            changed |= expected != recorded
            for relative in recorded:
                # A receipt is data, never a command or a path outside this checkout.
                directory = (repo / relative).resolve()
                if not directory.is_relative_to(repo):
                    raise ValueError('receipt source is outside the checkout')
                entry = record['Source:' + relative]
                changed |= source_digest(directory, entry['revision'], entry['paths'].splitlines()) != entry['digest']
            state['label'] = 'REBUILD NEEDED' if changed else 'READY'
            state['detail'] = ('Source files differ from the installed build. Play still uses the existing executable.'
                               if changed else 'The installed build matches the source files.')
        if update:
            state['label'] = 'UPDATE AVAILABLE'
            state['detail'] += ' A newer commit is available on the tracked branch.'
        if 'network' in state:
            state['detail'] += ' Update check: ' + state['network'] + '.'
    except (subprocess.SubprocessError, OSError, configparser.Error, ValueError):
        state['label'] = 'VERSION UNKNOWN'
        state['detail'] = 'Build information could not be verified. Play still uses the existing executable.'
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['write', 'status'])
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    repo, exe = args.repo.resolve(), args.exe.resolve()
    data = receipt(repo, exe) if args.action == 'write' else status(repo, exe, args.fetch)
    write_ini(args.output, data)


if __name__ == '__main__':
    main()
