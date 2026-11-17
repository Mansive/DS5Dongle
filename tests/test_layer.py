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


class LayerSetToolTests(unittest.TestCase):
    """Two layer buttons: the 0xFC payload followed by the 0xFD payload."""

    def table(self, layers, key):
        offset = tool.LAYER_TABLE_OFFSETS[key]
        return layers[offset:offset + tool.BUTTON_REMAP_COUNT]

    def test_sizes_match_firmware(self):
        self.assertEqual(tool.LAYER_SIZE, 29)
        self.assertEqual(tool.LAYER2_SIZE, 57)
        self.assertLessEqual(tool.LAYER2_SIZE, tool.SET_DATA_LEN)
        header = (ROOT / 'src/button_remap.h').read_text(encoding='utf-8')
        self.assertIn('combo_remap[BUTTON_REMAP_COUNT]', header)
        cmd = (ROOT / 'src/cmd.cpp').read_text(encoding='utf-8')
        self.assertIn('report_id == 0xfd', cmd)

    def test_buttons_and_tables(self):
        layers = tool.parse_layer_assignments(['buttons=LeftPaddle,RightPaddle'], tool.default_layers())
        self.assertEqual(tool.layer_buttons(layers), (B['leftpaddle'], B['rightpaddle']))
        layers = tool.parse_layer_assignments(list(tool.FACE_DPAD_PRESET), layers, 'a')
        layers = tool.parse_layer_assignments(['cross=l1'], layers, 'b')
        layers = tool.parse_layer_assignments(['cross=r3', 'circle=disable'], layers, 'ab')
        self.assertEqual(self.table(layers, 'a')[B['cross']], B['dpadsouth'])
        self.assertEqual(self.table(layers, 'b')[B['cross']], B['l1'])
        self.assertEqual(self.table(layers, 'ab')[B['cross']], B['r3'])
        self.assertEqual(self.table(layers, 'ab')[B['square']], B['square'])  # no merge from A
        self.assertEqual(bytes(tool.normalize_layers(layers)), bytes(layers))
        # b=off keeps the tables, clears only the button.
        off = tool.parse_layer_assignments(['b=off'], layers)
        self.assertEqual(tool.layer_buttons(off)[1], tool.LAYER_DISABLED)
        self.assertEqual(self.table(off, 'b')[B['cross']], B['l1'])

    def test_normalize_mirrors_firmware(self):
        layers = tool.default_layers()
        layers[0] = B['leftpaddle']
        layers[tool.LAYER_BUTTON_OFFSETS['b']] = B['leftpaddle']  # duplicate -> B off
        self.assertEqual(tool.layer_buttons(tool.normalize_layers(layers))[1], tool.LAYER_DISABLED)
        layers[tool.LAYER_BUTTON_OFFSETS['b']] = B['rightpaddle']
        layers[tool.LAYER_TABLE_OFFSETS['ab'] + B['leftpaddle']] = B['l1']  # layer button -> identity
        layers[tool.LAYER_TABLE_OFFSETS['a'] + B['rightpaddle']] = B['l2']
        layers[tool.LAYER_TABLE_OFFSETS['b'] + B['cross']] = 200  # out of range -> identity
        normalized = tool.normalize_layers(layers)
        self.assertEqual(self.table(normalized, 'ab')[B['leftpaddle']], B['leftpaddle'])
        self.assertEqual(self.table(normalized, 'a')[B['rightpaddle']], B['rightpaddle'])
        self.assertEqual(self.table(normalized, 'b')[B['cross']], B['cross'])
        # Erased flash for the B part reads as "off, identity".
        ff = bytes(tool.default_layer()) + b'\xff' * tool.LAYER2_SIZE
        self.assertEqual(bytes(tool.normalize_layers(ff)), bytes(tool.default_layers()))

    def test_layer_set_rejections(self):
        for tokens, table in ((['buttons=LeftPaddle,LeftPaddle'], 'a'),
                              (['buttons=LeftPaddle,RightPaddle', 'rightpaddle=l1'], 'ab'),
                              (['b=up'], 'a'), (['buttons=a,b,c'], 'a'), (['cross=l1'], 'x')):
            with self.assertRaises(SystemExit, msg=tokens):
                tool.parse_layer_assignments(tokens, tool.default_layers(), table)


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
    uint8_t guard_a = 0x5A; // detects writes past the first layer
    ButtonLayer2 layer2;
    uint8_t guard = 0x5A; // detects writes past the second layer
};
struct Storage { Button button; };
inline Storage storage;
inline Button& get_button() { return storage.button; }
void button_layer_valid();
''', encoding='utf-8')
            # The real validation/setter from config.cpp.
            functions = (extract_function(config, 'void button_layer_valid() {') +
                         extract_function(config, 'void set_button_layer(') +
                         extract_function(config, 'void set_button_layer2('))
            (tmp / 'config_layer.cpp').write_text(
                '#include "config.h"\n#include "button_utils.h"\n#include <initializer_list>\n' + functions,
                encoding='utf-8')
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
