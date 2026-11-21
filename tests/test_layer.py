"""Host regression for the button layers: python tests/test_layer.py (requires g++ on PATH)."""
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
    """Three layers, each a 3-byte hold bitmask and a 28-entry table."""

    def test_sizes_match_firmware(self):
        self.assertEqual(tool.LAYER_ENTRY_SIZE, 31)
        self.assertEqual(tool.LAYERS_SIZE, 93)
        self.assertLessEqual(tool.LAYER_REPORT1_SIZE, tool.SET_DATA_LEN)
        header = (ROOT / 'src/button_remap.h').read_text(encoding='utf-8')
        self.assertIn('BUTTON_LAYER_COUNT = 3', header)
        self.assertIn('BUTTON_LAYER_HOLD_BYTES = 3', header)

    def test_default_and_normalize(self):
        layers = tool.default_layers()
        self.assertEqual(len(layers), tool.LAYERS_SIZE)
        for number in tool.LAYER_NUMBERS:
            self.assertEqual(tool.layer_hold(layers, number), [])
            self.assertEqual(bytes(tool.layer_table(layers, number)), bytes(range(tool.BUTTON_REMAP_COUNT)))
        self.assertEqual(bytes(tool.normalize_layers(layers)), bytes(layers))

    def test_hold_and_tables(self):
        layers = tool.parse_layer_assignments(['hold=LeftPaddle'] + list(tool.FACE_DPAD_PRESET),
                                              tool.default_layers(), 1)
        layers = tool.parse_layer_assignments(['hold=RightPaddle', 'cross=l1'], layers, 2)
        layers = tool.parse_layer_assignments(['hold=LeftPaddle+RightPaddle', 'cross=r3', 'circle=disable'],
                                              layers, 3)
        self.assertEqual(tool.layer_hold(layers, 1), [B['leftpaddle']])
        self.assertEqual(tool.layer_hold(layers, 3), [B['leftpaddle'], B['rightpaddle']])
        self.assertEqual(tool.layer_table(layers, 1)[B['cross']], B['dpadsouth'])
        self.assertEqual(tool.layer_table(layers, 2)[B['cross']], B['l1'])
        self.assertEqual(tool.layer_table(layers, 3)[B['cross']], B['r3'])
        self.assertEqual(tool.layer_table(layers, 3)[B['square']], B['square'])  # no merge from layer 1
        self.assertEqual(bytes(tool.normalize_layers(layers)), bytes(layers))
        # Hold bitmask encoding: bit n = ButtonId Square + n, little endian.
        offset = tool.layer_offset(3)
        bits = (1 << (B['leftpaddle'] - B['square'])) | (1 << (B['rightpaddle'] - B['square']))
        self.assertEqual(bytes(layers[offset:offset + 3]), bits.to_bytes(3, 'little'))
        # hold=off keeps the table.
        off = tool.parse_layer_assignments(['hold=off'], layers, 2)
        self.assertEqual(tool.layer_hold(off, 2), [])
        self.assertEqual(tool.layer_table(off, 2)[B['cross']], B['l1'])

    def test_normalize_mirrors_firmware(self):
        layers = tool.default_layers()
        tool.set_layer_hold(layers, 1, [B['l1'], B['r1']])
        tool.set_layer_hold(layers, 3, [B['r1'], B['l1']])  # duplicate -> layer 3 off
        layers[tool.layer_offset(2) + 2] |= 0xF0  # bits past RightPaddle are dropped
        start = tool.layer_offset(2) + tool.LAYER_HOLD_BYTES
        layers[start + B['l1']] = B['l2']  # layer button -> identity
        layers[start + B['cross']] = 200  # out of range -> identity
        normalized = tool.normalize_layers(layers)
        self.assertEqual(tool.layer_hold(normalized, 3), [])
        self.assertEqual(tool.layer_hold(normalized, 2), [])
        self.assertEqual(tool.layer_table(normalized, 2)[B['l1']], B['l1'])
        self.assertEqual(tool.layer_table(normalized, 2)[B['cross']], B['cross'])

    def test_rejections(self):
        one = tool.parse_layer_assignments(['hold=LeftPaddle'], tool.default_layers(), 1)
        for tokens, layers, number in ((['hold=up'], tool.default_layers(), 1),
                                       (['hold=dpadsouth'], tool.default_layers(), 1),
                                       (['hold=LeftPaddle', 'leftpaddle=l1'], tool.default_layers(), 1),
                                       (['leftpaddle=l1'], one, 2),  # a layer button in any layer
                                       (['hold=LeftPaddle'], one, 2),  # same hold set as layer 1
                                       (['cross'], tool.default_layers(), 1),
                                       (['hold=nonsense'], tool.default_layers(), 1),
                                       (['cross=l1'], tool.default_layers(), 4)):
            with self.assertRaises(SystemExit, msg=tokens):
                tool.parse_layer_assignments(tokens, layers, number)


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
#include <cstddef>
#include "button_remap.h"
struct Button {
    uint8_t button_remap[BUTTON_REMAP_COUNT];
    ButtonLayers layers;
    uint8_t guard = 0x5A; // detects writes past the last layer
};
struct Storage { Button button; };
inline Storage storage;
inline Button& get_button() { return storage.button; }
constexpr uint16_t BUTTON_LAYER_REPORT1_SIZE = 2 * sizeof(ButtonLayer);
constexpr uint16_t BUTTON_LAYER_REPORT2_SIZE = sizeof(ButtonLayer);
void button_layer_valid();
''', encoding='utf-8')
            # The real validation/setter from config.cpp.
            functions = (extract_function(config, 'void button_layer_valid() {') +
                         extract_function(config, 'void set_button_layers('))
            (tmp / 'config_layer.cpp').write_text(
                '#include "config.h"\n#include "button_utils.h"\n' + functions,
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
