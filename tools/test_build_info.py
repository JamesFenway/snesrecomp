"""ROM-free build receipt regression tests, using temporary Git repositories."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).with_name('build_info.py')
spec = importlib.util.spec_from_file_location('build_info', HELPER)
info = importlib.util.module_from_spec(spec)
spec.loader.exec_module(info)


class BuildInfoTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / 'repo'
        self.repo.mkdir()
        self.git('init', '-b', 'main')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.invalid')
        (self.repo / 'src').mkdir()
        (self.repo / 'src/game.c').write_text('game\n')
        (self.repo / '.gitignore').write_text('build/\n*.sfc\n')
        self.commit()
        self.exe = self.repo / 'build/game'
        self.exe.parent.mkdir()
        self.exe.write_bytes(b'executable')

    def git(self, *args):
        return info.git(self.repo, *args).decode().strip()

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-m', 'test')

    def write(self):
        info.write_ini(self.exe.parent / 'build-info.ini', info.receipt(self.repo, self.exe))

    def label(self, fetch=False):
        return info.status(self.repo, self.exe, fetch)['Status']['label']

    def test_source_changes_and_committing_dirty_build(self):
        self.write()
        self.assertEqual(self.label(), 'READY')
        (self.repo / 'src/game.c').write_text('changed\n')
        self.assertEqual(self.label(), 'REBUILD NEEDED')
        (self.repo / 'src/new.c').write_text('new\n')
        self.write()
        self.commit()
        self.assertEqual(self.label(), 'READY')
        (self.repo / 'src/new.c').unlink()
        self.assertEqual(self.label(), 'REBUILD NEEDED')
        self.write()
        self.commit()
        self.assertEqual(self.label(), 'READY')

    def test_docs_rom_and_build_output_do_not_change_source(self):
        self.write()
        (self.repo / 'README.md').write_text('documentation')
        (self.repo / 'private.sfc').write_bytes(b'private fixture')
        (self.repo / 'build/save.sav').write_bytes(b'save')
        self.commit()
        self.assertEqual(self.label(), 'READY')
        self.exe.write_bytes(b'different executable')
        self.assertEqual(self.label(), 'REBUILD NEEDED')

    def test_old_build_missing_executable_and_export(self):
        self.assertEqual(self.label(), 'VERSION UNKNOWN')
        self.exe.unlink()
        self.assertEqual(self.label(), 'NOT BUILT')
        self.exe.write_bytes(b'export')
        exported = self.repo / 'export'
        exported.mkdir()
        # A source archive outside any Git checkout can still write a receipt.
        archive = Path(self.temp.name) / 'archive'
        archive.mkdir()
        record = info.receipt(archive, self.exe)
        self.assertEqual(record['Build']['revision'], 'unknown')

    def test_submodule_change(self):
        child = Path(self.temp.name) / 'child'
        child.mkdir()
        subprocess.run(['git', 'init', '-b', 'main', str(child)], check=True, capture_output=True)
        for key, value in [('user.name', 'Test'), ('user.email', 'test@example.invalid')]:
            info.git(child, 'config', key, value)
        (child / 'code.c').write_text('module')
        info.git(child, 'add', '.')
        info.git(child, 'commit', '-m', 'module')
        self.git('-c', 'protocol.file.allow=always', 'submodule', 'add', str(child), 'framework')
        self.commit()
        self.write()
        self.assertEqual(self.label(), 'READY')
        (self.repo / 'framework/code.c').write_text('changed')
        self.assertEqual(self.label(), 'REBUILD NEEDED')

    def test_remote_update_and_offline_cached_status(self):
        remote = Path(self.temp.name) / 'remote'
        subprocess.run(['git', 'clone', '--bare', str(self.repo), str(remote)], check=True, capture_output=True)
        self.git('remote', 'add', 'origin', str(remote))
        self.git('fetch', 'origin')
        self.git('branch', '--set-upstream-to=origin/main')
        self.write()
        self.assertEqual(self.label(True), 'READY')
        # Publish one commit without changing this checkout.
        clone = Path(self.temp.name) / 'clone'
        subprocess.run(['git', 'clone', str(remote), str(clone)], check=True, capture_output=True)
        info.git(clone, 'config', 'user.name', 'Test')
        info.git(clone, 'config', 'user.email', 'test@example.invalid')
        (clone / 'src/game.c').write_text('upstream')
        info.git(clone, 'commit', '-am', 'upstream')
        info.git(clone, 'push')
        before = self.git('rev-parse', 'HEAD')
        self.assertEqual(self.label(True), 'UPDATE AVAILABLE')
        self.assertEqual(self.git('rev-parse', 'HEAD'), before)
        self.git('remote', 'set-url', 'origin', str(remote / 'missing'))
        state = info.status(self.repo, self.exe, True)['Status']
        self.assertEqual(state['label'], 'UPDATE AVAILABLE')
        self.assertIn('offline', state['detail'])


if __name__ == '__main__':
    unittest.main()
