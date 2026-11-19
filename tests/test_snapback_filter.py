"""Host regression for the snapback filter: python tests/test_snapback_filter.py (requires g++ on PATH)."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('config_tool', ROOT / 'tools/config_tool.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def extract(text, start, end_marker):
    begin = text.index(start)
    stop = text.index(end_marker, begin) + len(end_marker)
    return text[begin:stop]


class SnapbackToolTests(unittest.TestCase):
    def test_config_layout_matches_firmware(self):
        header = (ROOT / 'src/config.h').read_text(encoding='utf-8')
        body = extract(header, 'struct __attribute__((packed)) Config_body {', '};')
        fields = re.findall(r'^\s*(?:uint8_t|float)\s+(\w+);', body, re.M)
        self.assertEqual(fields, tool.FIELD_NAMES)
        self.assertEqual(tool.FIELD_NAMES[-2:], ['snapback_left', 'snapback_right'])
        self.assertEqual(tool.BODY_SIZE, 29)

    def test_config_values(self):
        filt = (ROOT / 'src/snapback_filter.h').read_text(encoding='utf-8')
        limit = int(re.search(r'SNAPBACK_HOLD_MAX_MS = (\d+);', filt).group(1))
        self.assertEqual(tool.parse_assignment(f'snapback_left={limit}'), ('snapback_left', limit))
        self.assertEqual(tool.parse_assignment('snapback_right=0'), ('snapback_right', 0))
        with self.assertRaises(SystemExit):
            tool.parse_assignment(f'snapback_right={limit + 1}')
        # Older firmware reads back without the new fields; they pad to 0 (off).
        cfg = tool.unpack_config(bytes(tool.BODY_SIZE))
        cfg['haptics_gain'] = 1.0
        cfg['audio_buffer_length'] = 48
        self.assertEqual(tool.pack_config(cfg)[-2:], b'\x00\x00')


class SnapbackFirmwareTests(unittest.TestCase):
    def test_firmware(self):
        utils = (ROOT / 'src/utils.h').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name in ('snapback_filter.cpp', 'snapback_filter.h'):
                shutil.copyfile(ROOT / 'src' / name, tmp / name)
            types = extract(utils, 'enum PowerState', 'uint8_t AesCmac[8];\n};')
            stub = '#pragma once\n#include <cstdint>\n#define __not_in_flash_func(name) name\n'
            (tmp / 'utils.h').write_text(stub + types + '\n', encoding='utf-8')
            shutil.copyfile(ROOT / 'tests/snapback_filter_test.cpp', tmp / 'test.cpp')
            exe = tmp / 'test.exe'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(tmp),
                            str(tmp / 'snapback_filter.cpp'), str(tmp / 'test.cpp'), '-o', str(exe)],
                           check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            print(result.stdout, end='')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
