"""Host regression for the button layer: python tests/test_layer.py (requires g++ on PATH)."""
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

B = tool.BUTTON_NAME_TO_ID


def extract(text, start, end_marker):
    """Return text from `start` through the first `end_marker` after it."""
    begin = text.index(start)
    stop = text.index(end_marker, begin) + len(end_marker)
    return text[begin:stop]


def extract_function(text, signature):
    """Return a top-level C++ function: from its signature to the closing brace in column 0."""
    match = re.search(re.escape(signature) + r'.*?\n}\n', text, re.S)
    if not match:
        raise AssertionError(f'{signature!r} not found')
    return match.group(0)


class LayerToolTests(unittest.TestCase):
    def test_default_and_normalize(self):
        layer = tool.default_layer()
        self.assertEqual(len(layer), tool.LAYER_SIZE)
        self.assertEqual(layer[0], tool.LAYER_DISABLED)
        self.assertEqual(bytes(tool.normalize_layer(b'\xff' * tool.LAYER_SIZE)), bytes(layer))
        odd = bytearray(layer)
        odd[0] = B['dpadnorth']
        odd[1 + B['cross']] = 200
        self.assertEqual(bytes(tool.normalize_layer(odd)), bytes(layer))

    def test_face_dpad_assignments(self):
        layer = tool.parse_layer_assignments(
            ['modifier=LeftPaddle'] + list(tool.FACE_DPAD_PRESET), tool.default_layer())
        self.assertEqual(layer[0], B['leftpaddle'])
        self.assertEqual(layer[1 + B['triangle']], B['dpadnorth'])
        self.assertEqual(layer[1 + B['cross']], B['dpadsouth'])
        self.assertEqual(layer[1 + B['square']], B['dpadwest'])
        self.assertEqual(layer[1 + B['circle']], B['dpadeast'])
        self.assertEqual(bytes(tool.normalize_layer(layer)), bytes(layer))
        cleared = tool.parse_layer_assignments(['cross=nomap', 'modifier=off'], layer)
        self.assertEqual(cleared[0], tool.LAYER_DISABLED)
        self.assertEqual(cleared[1 + B['cross']], B['cross'])

    def test_rejections(self):
        for tokens in (['modifier=up'], ['modifier=dpadsouth'], ['modifier=LeftPaddle', 'leftpaddle=l1'],
                       ['cross'], ['modifier=nonsense']):
            with self.assertRaises(SystemExit, msg=tokens):
                tool.parse_layer_assignments(tokens, tool.default_layer())


class BootselCommandTests(unittest.TestCase):
    def test_payload_matches_firmware(self):
        payload = tool.bootsel_command()
        self.assertEqual(len(payload), tool.SET_DATA_LEN)
        self.assertEqual(payload[:3], bytes([0x06, 0xB0, 0x07]))
        cmd_h = (ROOT / 'src/cmd.h').read_text(encoding='utf-8')
        self.assertIn('PICO_CMD_REBOOT_BOOTSEL = 0x06', cmd_h)
        self.assertIn('PICO_CMD_REBOOT_BOOTSEL_MAGIC[2] = {0xB0, 0x07}', cmd_h)


class LayerFirmwareTests(unittest.TestCase):
    def test_firmware(self):
        utils = (ROOT / 'src/utils.h').read_text(encoding='utf-8')
        config = (ROOT / 'src/config.cpp').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            # Compile the real remap code; only the platform-heavy headers are replaced.
            for name in ('button_remap.cpp', 'button_remap.h', 'button_utils.cpp', 'button_utils.h'):
                shutil.copyfile(ROOT / 'src' / name, tmp / name)
            # The real report layout (Direction, PowerState, touch data, USBGetStateData),
            # without utils.h's BTstack/Pico includes.
            types = extract(utils, 'enum PowerState', 'uint8_t AesCmac[8];\n};')
            (tmp / 'utils.h').write_text('#pragma once\n#include <cstdint>\n' + types + '\n',
                                         encoding='utf-8')
            (tmp / 'config.h').write_text('''#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "button_remap.h"
struct Button {
    uint8_t button_remap[BUTTON_REMAP_COUNT];
    ButtonLayer layer;
    uint8_t guard = 0x5A; // detects writes past the layer
};
struct Storage { Button button; };
inline Storage storage;
inline Button& get_button() { return storage.button; }
void button_layer_valid();
''', encoding='utf-8')
            # The real validation/setter from config.cpp.
            functions = (extract_function(config, 'void button_layer_valid() {') +
                         extract_function(config, 'void set_button_layer('))
            (tmp / 'config_layer.cpp').write_text(
                '#include "config.h"\n#include "button_utils.h"\n' + functions, encoding='utf-8')
            shutil.copyfile(ROOT / 'tests/layer_remap_test.cpp', tmp / 'test.cpp')
            exe = tmp / 'test.exe'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(tmp),
                            str(tmp / 'button_remap.cpp'), str(tmp / 'button_utils.cpp'),
                            str(tmp / 'config_layer.cpp'), str(tmp / 'test.cpp'),
                            '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            print(result.stdout, end='')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
