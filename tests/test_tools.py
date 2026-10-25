"""Regression tests for install rollback/path validation and AT verdicts."""
import contextlib
import hashlib
import importlib.util
import io
import os
import struct
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from datetime import datetime, timezone, timedelta
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('UNIKNECT_TEST_OUTPUT', ROOT / '.tmp/tests/tool_tests'))
OUT.mkdir(parents=True, exist_ok=True)


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


setup = load('setup_toolchain')
probe = load('probe_modem')
clock_sync = load('sync_phone_time')
lcd_benchmark = load('benchmark_lcd')
builder = load('build')


class FlashBoundaryTests(unittest.TestCase):
    def image(self,address):
        data=bytearray(88);data[:6]=b'\x7fELF\x01\x01'
        struct.pack_into('<I',data,28,52);struct.pack_into('<HH',data,42,32,1)
        struct.pack_into('<8I',data,52,1,84,address,address,4,4,5,4)
        return data

    def test_application_image_allowed(self):
        with tempfile.NamedTemporaryFile(dir=OUT,suffix='.elf',delete=False) as f:
            f.write(self.image(0x08000000));name=f.name
        try:builder.validate_flash_image(name)
        finally:Path(name).unlink()

    def test_settings_overlap_rejected(self):
        with tempfile.NamedTemporaryFile(dir=OUT,suffix='.elf',delete=False) as f:
            f.write(self.image(0x0813fffe));name=f.name
        try:
            with self.assertRaisesRegex(ValueError,'settings'):builder.validate_flash_image(name)
        finally:Path(name).unlink()


class LcdBenchmarkTests(unittest.TestCase):
    def samples(self):
        return [dict(bench=1,rotation=0,profile=14,window=5000,received_ms=i*5000,
                     fps10=450,rolling10=440,timeout=0,dmaerr=0) for i in range(12)]

    def test_requires_complete_sixty_seconds(self):
        self.assertTrue(lcd_benchmark.evaluate_samples(self.samples(),1,0,14,60,40)[-1])
        self.assertFalse(lcd_benchmark.evaluate_samples(self.samples()[:-1],1,0,14,60,40)[-1])

    def test_rolling_floor_and_errors_are_required(self):
        for field,value in [('rolling10',399),('timeout',1),('dmaerr',1)]:
            with self.subTest(field=field):
                samples=self.samples();samples[6][field]=value
                self.assertFalse(lcd_benchmark.evaluate_samples(samples,1,0,14,60,40)[-1])

    def test_missing_window_and_wrong_scene_cannot_pass(self):
        samples=self.samples();samples[-1]['received_ms']+=8000
        self.assertFalse(lcd_benchmark.evaluate_samples(samples,1,0,14,60,40)[-1])
        self.assertFalse(lcd_benchmark.evaluate_samples(self.samples(),2,0,14,60,40)[-1])


class ClockSyncTests(unittest.TestCase):
    def test_beijing_midnight_from_utc(self):
        self.assertEqual(clock_sync.clock_command(datetime(2026,10,22,16,1,2,tzinfo=timezone.utc)),
                         'clock 2026-10-23 00:01:02')

    def test_source_timezone_does_not_change_instant(self):
        west=timezone(timedelta(hours=-7))
        self.assertEqual(clock_sync.clock_command(datetime(2026,10,22,9,1,2,tzinfo=west)),
                         'clock 2026-10-23 00:01:02')
        with self.assertRaises(ValueError):
            clock_sync.clock_command(datetime(2026,10,22,9,1,2))


class InstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=OUT)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        assert self.base.is_relative_to(OUT.resolve())
        self.dest = self.base / 'toolchain'
        self.dest.mkdir()
        self.marker = self.dest / 'old.txt'
        self.marker.write_text('old')
        self.archive = self.base / 'input.zip'

    def archive_with(self, extra=(), compiler=True):
        with zipfile.ZipFile(self.archive, 'w') as zf:
            if compiler:
                zf.writestr('top/bin/arm-none-eabi-gcc.exe', 'test compiler, never executed')
            for name in extra:
                zf.writestr(name, 'test')
        return hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def install(self, digest):
        with contextlib.redirect_stdout(io.StringIO()):
            return setup.install_toolchain(self.archive, self.dest, digest, self.base)

    def test_escape_is_rejected_and_old_install_survives(self):
        for name in ['top/../escaped.txt', 'top/../../escaped.txt', '/absolute.txt',
                     'top/C:/escaped.txt', 'top\\..\\escaped.txt', 'top/CON.txt', 'other/file']:
            with self.subTest(name=name):
                digest = self.archive_with([name])
                with self.assertRaises(ValueError):
                    self.install(digest)
                self.assertTrue(self.marker.is_file())
                self.assertTrue(self.archive.is_file())
                self.assertFalse((self.base / 'escaped.txt').exists())

    def test_bad_hash_preserves_installation(self):
        self.archive_with()
        with self.assertRaises(ValueError):
            self.install('0' * 64)
        self.assertTrue(self.marker.is_file())

    def test_missing_compiler_preserves_installation(self):
        digest = self.archive_with(['top/readme.txt'], compiler=False)
        with self.assertRaises(ValueError):
            self.install(digest)
        self.assertTrue(self.marker.is_file())

    def test_success_replaces_only_after_validation(self):
        digest = self.archive_with()
        self.assertEqual(self.install(digest), 0)
        self.assertFalse(self.marker.exists())
        self.assertTrue((self.dest / 'bin/arm-none-eabi-gcc.exe').is_file())
        self.assertFalse(self.archive.exists())
        self.assertFalse(self.dest.with_name('toolchain.previous').exists())

    def test_failed_swap_restores_previous_install(self):
        digest = self.archive_with()
        original = Path.rename

        def rename(path, target):
            if path.name.startswith('.toolchain-'):
                raise OSError('simulated replacement failure')
            return original(path, target)

        with patch.object(Path, 'rename', rename):
            with self.assertRaises(OSError):
                self.install(digest)
        self.assertEqual(self.marker.read_text(), 'old')
        self.assertTrue(self.archive.is_file())

    def test_destination_outside_workspace_rejected(self):
        digest = self.archive_with()
        with self.assertRaises(ValueError):
            setup.install_toolchain(self.archive, self.base.parent / 'outside', digest, self.base)


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.remaining = b''
    def __enter__(self): return self
    def __exit__(self, *args): return False
    def reset_input_buffer(self): self.remaining = b''
    def write(self, data):
        cmd = data.strip()
        self.remaining = {b'AT': b'BODY LOOK OK HERE\r\nOK\r\n',
                          b'BAD': b'+CME ERROR: 58\r\n',
                          b'SILENT': b'LOOK OK HERE\r\n'}[cmd]
    def flush(self): pass
    def read(self, size):
        chunk, self.remaining = self.remaining[:3], self.remaining[3:]
        return chunk


class ProbeTests(unittest.TestCase):
    def test_complete_final_lines_only(self):
        self.assertIsNone(probe.terminal_status(b'TOKEN OK HERE\r\n'))
        self.assertIsNone(probe.terminal_status(b'\r\nOK'))
        self.assertEqual(probe.terminal_status(b'\r\nOK\r\n'), 'ok')
        self.assertEqual(probe.terminal_status(b'\r\n+CMS ERROR: 500\r\n'), 'error')

    def test_query_returns_explicit_error_and_timeout(self):
        with patch.object(probe.serial, 'Serial', FakeSerial), contextlib.redirect_stdout(io.StringIO()):
            result = probe.query('FAKE', 115200, ['AT', 'BAD', 'SILENT'], wait=0, timeout=.01)
        self.assertEqual([r.status for r in result], ['ok', 'error', 'timeout'])
        self.assertEqual([r.accepted for r in result], [True, False, False])

    def test_allow_error_never_accepts_timeout(self):
        with patch.object(probe.serial, 'Serial', FakeSerial), contextlib.redirect_stdout(io.StringIO()):
            result = probe.query('FAKE', 115200, ['BAD', 'SILENT'], wait=0,
                                 timeout=.01, allow_error=['BAD', 'SILENT'])
        self.assertTrue(result[0].accepted)
        self.assertFalse(result[1].accepted)

    def test_main_exit_status_reflects_failure(self):
        with patch.object(probe, 'query', return_value=[probe.QueryResult('AT', 'timeout', '', False)]):
            self.assertEqual(probe.main(['--cmd', 'AT']), 1)
        with patch.object(probe, 'query', return_value=[probe.QueryResult('AT', 'ok', 'OK', True)]):
            self.assertEqual(probe.main(['--cmd', 'AT']), 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
