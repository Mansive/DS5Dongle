"""Host regression for the touchpad mouse: python tests/test_touchpad_mouse.py (requires g++ on PATH)."""
import importlib.util
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('config_tool', ROOT / 'tools/config_tool.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

B = tool.BUTTON_NAME_TO_ID


def extract(text, start, end_marker):
    begin = text.index(start)
    stop = text.index(end_marker, begin) + len(end_marker)
    return text[begin:stop]


def extract_function(text, signature):
    match = re.search(re.escape(signature) + r'.*?\n}\n', text, re.S)
    if not match:
        raise AssertionError(f'{signature!r} not found')
    return match.group(0)


class TouchpadToolTests(unittest.TestCase):
    def test_config_layout_matches_firmware(self):
        header = (ROOT / 'src/config.h').read_text(encoding='utf-8')
        body = extract(header, 'struct __attribute__((packed)) Config_body {', '};')
        fields = re.findall(r'^\s*(?:uint8_t|float)\s+(\w+);', body, re.M)
        self.assertEqual(fields, tool.FIELD_NAMES)
        self.assertEqual(tool.FIELD_NAMES.index('touchpad_mouse'), 22)
        self.assertEqual(tool.FIELD_NAMES.index('touchpad_sensitivity'), 23)

    def test_config_values(self):
        self.assertEqual(tool.parse_assignment('touchpad_mouse=2'), ('touchpad_mouse', 2))
        self.assertEqual(tool.parse_assignment('touchpad_sensitivity=20'), ('touchpad_sensitivity', 20))
        for token in ('touchpad_mouse=3', 'touchpad_sensitivity=21'):
            with self.assertRaises(SystemExit, msg=token):
                tool.parse_assignment(token)
        # Older firmware reads back without the new fields; they pad to 0 and still pack.
        cfg = tool.unpack_config(bytes(tool.BODY_SIZE))
        cfg['haptics_gain'] = 1.0
        cfg['audio_buffer_length'] = 48
        packed = tool.pack_config(cfg)
        self.assertEqual(packed[26:28], b'\x00\x00')

    def test_mouse_targets(self):
        utils = (ROOT / 'src/button_utils.h').read_text(encoding='utf-8')
        enum = extract(utils, 'enum ButtonId', '};')
        names = re.findall(r'^\s*(\w+)(?: = \d+)?,', enum, re.M)
        self.assertEqual(names[names.index('Disable') + 1:-1], list(tool.MOUSE_TARGET_NAMES))
        self.assertEqual(names[-1], 'RemapTargetCount')
        self.assertEqual(tool.parse_remap_assignment('r1=mouseleft'), (B['r1'], 28))
        self.assertEqual(tool.parse_remap_assignment('l1=RightClick'), (B['l1'], 29))
        self.assertEqual(tool.parse_remap_assignment('pad=middleclick'), (B['pad'], 30))
        self.assertEqual(tool.parse_remap_assignment('l3=mouse4'), (B['l3'], 31))
        self.assertEqual(tool.parse_remap_assignment('r3=MouseForward'), (B['r3'], 32))
        self.assertEqual(tool.button_name(32), 'MouseForward')
        self.assertEqual(tool.button_name(33), 'Invalid(33)')
        # Mouse buttons are targets only.
        for token in ('mouseleft=cross', 'shortcut'):
            with self.assertRaises(SystemExit, msg=token):
                tool.parse_remap_assignment(token)
        with self.assertRaises(SystemExit):
            tool.parse_layer_hold('MouseLeft')
        with self.assertRaises(SystemExit):
            tool.parse_shortcut_assignment('1=MouseLeft:F13')

    def test_layer_mouse_targets_survive_normalize(self):
        layers = tool.parse_layer_assignments(['hold=LeftPaddle', 'cross=mouseleft', 'circle=mouseback'],
                                              tool.default_layers(), 1)
        self.assertEqual(tool.layer_table(layers, 1)[B['cross']], 28)
        self.assertEqual(tool.layer_table(layers, 1)[B['circle']], 31)
        start = tool.layer_offset(1) + tool.LAYER_HOLD_BYTES
        layers[start + B['square']] = 33  # past MouseForward -> identity
        normalized = tool.normalize_layers(layers)
        self.assertEqual(normalized[start + B['square']], B['square'])
        self.assertEqual(normalized[start + B['cross']], 28)

    def test_toggle_shortcut(self):
        header = (ROOT / 'src/button_shortcut.h').read_text(encoding='utf-8')
        actions = re.findall(r'^\s*(ShortcutAction\w+)', extract(header, 'enum ShortcutAction', '};'), re.M)
        self.assertEqual(actions.index('ShortcutActionToggleTouchpadMouse'),
                         tool.SHORTCUT_ACTION_TOGGLE_TOUCHPAD_MOUSE)
        slot, shortcut = tool.parse_shortcut_assignment('7=Pad*2:toggle_touchpad_mouse')
        self.assertEqual(slot, 6)
        self.assertEqual(shortcut, (B['pad'], tool.TRIGGER_DOUBLE_TAP,
                                    tool.SHORTCUT_ACTION_TOGGLE_TOUCHPAD_MOUSE, [0, 0, 0], 0))
        self.assertTrue(tool.shortcut_slot_valid(shortcut))
        with self.assertRaises(SystemExit):
            tool.parse_shortcut_assignment('7=Pad@hold:toggle_touchpad_mouse')


class TouchpadDescriptorTests(unittest.TestCase):
    def test_consumer_descriptor_length(self):
        text = (ROOT / 'src/usb_descriptors.cpp').read_text(encoding='utf-8')
        body = extract(text, 'uint8_t const desc_hid_report_consumer[] = {', '};')
        body = re.sub(r'//[^\n]*', '', body.split('{', 1)[1].rsplit('}', 1)[0])
        size = len([item for item in body.split(',') if item.strip()])  # one byte per item
        self.assertEqual(size, 110)
        self.assertIn('0x6E, 0x00, // wDescriptorLength: 110', text)
        self.assertIn('sizeof(desc_hid_report_consumer) == 110', text)


class TouchpadFirmwareTests(unittest.TestCase):
    def test_firmware(self):
        utils = (ROOT / 'src/utils.h').read_text(encoding='utf-8')
        config = (ROOT / 'src/config.cpp').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name in ('button_remap.cpp', 'button_remap.h', 'button_utils.cpp', 'button_utils.h',
                         'touchpad_mouse.cpp', 'touchpad_mouse.h'):
                shutil.copyfile(ROOT / 'src' / name, tmp / name)
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
};
struct Storage { Button button; };
inline Storage storage;
inline Button& get_button() { return storage.button; }
void button_layer_valid();
''', encoding='utf-8')
            (tmp / 'config_layer.cpp').write_text(
                '#include "config.h"\n#include "button_utils.h"\n' +
                extract_function(config, 'void button_layer_valid() {'), encoding='utf-8')
            shutil.copyfile(ROOT / 'tests/touchpad_mouse_test.cpp', tmp / 'test.cpp')
            exe = tmp / 'test.exe'
            # Without ENABLE_WAKE_HID only the USB-free parts of touchpad_mouse.cpp compile.
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(tmp),
                            str(tmp / 'button_remap.cpp'), str(tmp / 'button_utils.cpp'),
                            str(tmp / 'touchpad_mouse.cpp'), str(tmp / 'config_layer.cpp'),
                            str(tmp / 'test.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            print(result.stdout, end='')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
