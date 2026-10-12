"""Behavior tests on real Linux files and pseudo-terminals (stdlib only)."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import socket
import struct
import subprocess
import tempfile
import termios
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = str(ROOT / 'bin/ls')
MINOR = int((ROOT / 'VERSION').read_text().strip().split('.')[1])
ANSI = re.compile(r'\x1b\[[0-9;]*m')
ENV = dict(os.environ, LC_ALL='C', TZ='UTC')


class ListingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='a02-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)

    def run_ls(self, *args, check=True):
        p = subprocess.run([BIN, *map(str, args)], cwd=self.base, env=ENV,
                           text=True, capture_output=True, timeout=10)
        if check:
            self.assertEqual(p.returncode, 0, p.stderr)
        return p

    def terminal(self, width, *args):
        master, slave = pty.openpty()
        try:
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, width, 0, 0))
            p = subprocess.Popen([BIN, *args], cwd=self.base, env=ENV,
                                 stdout=slave, stderr=subprocess.PIPE)
            os.close(slave)
            slave = -1
            data = b''
            while True:
                try:
                    part = os.read(master, 65536)
                    if not part:
                        break
                    data += part
                except OSError as ex:
                    if ex.errno != errno.EIO:
                        raise
                    break
            error = p.communicate(timeout=10)[1]
            self.assertEqual(p.returncode, 0, error)
            return ANSI.sub('', data.decode()).replace('\r\n', '\n')
        finally:
            os.close(master)
            if slave >= 0:
                os.close(slave)

    def fixtures(self):
        (self.base / 'ordinary').write_text('abc')
        (self.base / 'ordinary').chmod(0o640)
        (self.base / 'directory').mkdir()
        (self.base / 'run').touch(mode=0o755)
        (self.base / 'archive.tar').touch()
        (self.base / 'link').symlink_to('ordinary')
        (self.base / 'dangling').symlink_to('missing')
        os.mkfifo(self.base / 'pipe')
        (self.base / '.hidden').touch()

    def test_long_matches_gnu_metadata(self):
        self.fixtures()
        actual = ANSI.sub('', self.run_ls('-l').stdout).splitlines()
        expected = subprocess.check_output(
            ['/bin/ls', '-l', '--color=never'], cwd=self.base, env=ENV, text=True).splitlines()
        self.assertEqual(actual[0], expected[0])
        self.assertEqual(sorted(actual[1:]), sorted(expected[1:]))

    def test_special_permission_bits(self):
        for name, mode in [('lower', 0o7755), ('upper', 0o7644)]:
            p = self.base / name
            p.touch()
            p.chmod(mode)
            actual = ANSI.sub('', self.run_ls('-l', name).stdout).split()[0]
            expected = subprocess.check_output(['/bin/ls', '-ld', name], cwd=self.base,
                                               env=ENV, text=True).split()[0]
            self.assertEqual(actual, expected)

    def test_old_timestamp(self):
        p = self.base / 'old'
        p.touch()
        os.utime(p, (946684800, 946684800))
        self.assertIn('Jan  1  2000', self.run_ls('-l', 'old').stdout)

    def test_file_operand_and_space(self):
        (self.base / 'two words').write_text('hello')
        self.assertIn('two words', self.run_ls('-l', 'two words').stdout)

    def test_device(self):
        actual = ANSI.sub('', self.run_ls('-l', '/dev/null').stdout)
        expected = subprocess.check_output(['/bin/ls', '-l', '/dev/null'], env=ENV, text=True)
        self.assertEqual(actual, expected)

    def test_empty(self):
        self.assertEqual(self.run_ls().stdout, '')
        self.assertEqual(self.run_ls('-l').stdout, 'total 0\n')

    def test_hidden_files(self):
        (self.base / '.secret').touch()
        (self.base / 'visible').touch()
        self.assertNotIn('.secret', self.run_ls().stdout)
        self.assertIn('visible', self.run_ls().stdout)

    def test_error_and_continue(self):
        (self.base / 'valid').mkdir()
        (self.base / 'valid' / 'found').touch()
        result = self.run_ls('missing', 'valid', check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing', result.stderr)
        self.assertIn('found', result.stdout)

    def test_invalid_option(self):
        self.assertEqual(self.run_ls('-z', check=False).returncode, 2)

    @unittest.skipUnless(MINOR >= 2, 'columns introduced in 1.2')
    def test_terminal_widths(self):
        for name in 'abcdefg':
            (self.base / name).touch()
        # Derive filesystem order before sorting is introduced.
        names = sorted(os.listdir(self.base)) if MINOR >= 4 else os.listdir(self.base)
        for width in (1, 9, 18, 80):
            columns = min(len(names), max(1, width // 3))
            rows = (len(names) + columns - 1) // columns
            expected = [[names[i] for i in range(row, len(names), rows)] for row in range(rows)]
            actual = [line.split() for line in self.terminal(width).splitlines()]
            self.assertEqual(actual, expected, width)

    @unittest.skipUnless(MINOR >= 2, 'columns introduced in 1.2')
    def test_filename_wider_than_terminal(self):
        name = 'long_filename_' * 8
        (self.base / name).touch()
        self.assertEqual(self.terminal(5), name + '\n')

    @unittest.skipUnless(MINOR >= 3, 'horizontal introduced in 1.3')
    def test_horizontal(self):
        for name in 'abcdefg':
            (self.base / name).touch()
        names = sorted(os.listdir(self.base)) if MINOR >= 4 else os.listdir(self.base)
        actual = [line.split() for line in self.terminal(9, '-x').splitlines()]
        self.assertEqual(actual, [names[i:i+3] for i in range(0, 7, 3)])

    @unittest.skipUnless(MINOR >= 3, 'horizontal introduced in 1.3')
    def test_last_display_option_wins(self):
        (self.base / 'alpha').touch()
        self.assertTrue(self.run_ls('-xl').stdout.startswith('total '))
        self.assertFalse(self.run_ls('-lx').stdout.startswith('total '))

    @unittest.skipUnless(MINOR >= 4, 'sorting introduced in 1.4')
    def test_sort_all_modes(self):
        names = ['zulu', 'Beta', 'alpha', 'archive', 'Omega']
        for name in names:
            (self.base / name).touch()
        self.assertEqual(self.terminal(1).splitlines(), sorted(names))
        self.assertEqual(self.run_ls('-x').stdout.split(), sorted(names))
        rows = self.run_ls('-l').stdout.splitlines()[1:]
        self.assertEqual([row.split()[-1] for row in rows], sorted(names))

    @unittest.skipUnless(MINOR >= 5, 'colors introduced in 1.5')
    def test_colors_and_reset(self):
        self.fixtures()
        sock = socket.socket(socket.AF_UNIX)
        self.addCleanup(sock.close)
        sock.bind(str(self.base / 'socket'))
        out = self.run_ls().stdout
        for name, color in [('directory','0;34'), ('run','0;32'), ('archive.tar','0;31'),
                            ('link','0;35'), ('dangling','0;35'), ('pipe','7'), ('socket','7')]:
            self.assertIn('\x1b[' + color + 'm' + name + '\x1b[0m', out)
        (self.base / 'fake.zip.txt').touch()
        self.assertNotIn('\x1b[0;31mfake.zip.txt', self.run_ls().stdout)

    @unittest.skipUnless(MINOR >= 5, 'colors introduced in 1.5')
    def test_color_column_alignment(self):
        for name in ['aa','bb','cc','dd','ee','ff','gg']:
            (self.base / name).mkdir()
        lines = self.terminal(12).splitlines()
        self.assertEqual(lines, ['aa  dd  gg', 'bb  ee', 'cc  ff'])

    @unittest.skipUnless(MINOR >= 6, 'recursion introduced in 1.6')
    def test_recursive_headers_and_symlink_loop(self):
        (self.base / 'a' / 'nested').mkdir(parents=True)
        (self.base / 'b').mkdir()
        (self.base / 'a' / 'nested' / 'leaf').touch()
        (self.base / 'a' / 'back').symlink_to('..')
        (self.base / '.hidden_dir').mkdir()
        for option in ['-R', '-lR', '-xR']:
            out = ANSI.sub('', self.run_ls(option).stdout)
            headers = [line for line in out.splitlines() if line.endswith(':')]
            self.assertEqual(headers, ['.:', './a:', './a/nested:', './b:'])
            self.assertIn('leaf', out)
            self.assertNotIn('.hidden_dir', out)

    @unittest.skipUnless(MINOR >= 6, 'recursion introduced in 1.6')
    def test_recursive_path_from_another_cwd(self):
        (self.base / 'parent' / 'child').mkdir(parents=True)
        (self.base / 'parent' / 'child' / 'marker').touch()
        out = self.run_ls('-R', 'parent').stdout
        self.assertIn('parent/child:', out)
        self.assertIn('marker', out)

    def test_unreadable_directory(self):
        if os.geteuid() == 0:
            self.skipTest('root bypasses directory permissions')
        p = self.base / 'denied'
        p.mkdir()
        p.chmod(0)
        try:
            self.assertNotEqual(self.run_ls(p, check=False).returncode, 0)
        finally:
            p.chmod(0o700)


if __name__ == '__main__':
    unittest.main(verbosity=2)
