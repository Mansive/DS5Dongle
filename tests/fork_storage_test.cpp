// Host test for loading ForkStorage (flash page 1) and moving layers saved by
// older firmware off the config page. Built by tests/test_layer.py, which puts
// the real loader from src/config.cpp in front of this file and points
// XIP_BASE at the `flash` array.
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static ButtonLayers sample_layers() {
    ButtonLayers layers;
    layers.format = BUTTON_LAYER_FORMAT;
    for (uint8_t n = 0; n < BUTTON_LAYER_COUNT; n++) {
        layers.layer[n].hold[0] = static_cast<uint8_t>(1u << n);
        layers.layer[n].hold[1] = 0;
        layers.layer[n].hold[2] = 0;
        for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
            layers.layer[n].remap[i] = static_cast<uint8_t>((i + n + 1) % BUTTON_REMAP_COUNT);
        }
    }
    return layers;
}

static void erase() { std::memset(flash, 0xFF, sizeof(flash)); }

// Page 0 as saved by firmware that kept the layers in ConfigStorage: the crc32
// covers the body through the end of the layers.
static void write_legacy_page(const ButtonLayers &layers, bool good_crc) {
    Config config;
    std::memset(&config, 0x11, sizeof(config));
    config.magic = CONFIG_MAGIC;
    std::memcpy(flash, &config, sizeof(config));
    std::memset(flash + sizeof(config), 0x22, LEGACY_LAYERS_OFFSET - sizeof(config));
    std::memcpy(flash + LEGACY_LAYERS_OFFSET, &layers, sizeof(layers));
    const uint32_t body = offsetof(Config, body);
    uint32_t crc = crc32(flash + body, LEGACY_STORAGE_SIZE - body);
    if (!good_crc) crc ^= 1;
    std::memcpy(flash + offsetof(Config, crc32), &crc, sizeof(crc));
}

static void write_fork_page(const ButtonLayers &layers, bool good_crc) {
    ForkStorage fork;
    fork.magic = FORK_STORAGE_MAGIC;
    fork.layers = layers;
    fork.crc32 = calc_fork_crc(fork) ^ (good_crc ? 0u : 1u);
    std::memcpy(flash + FORK_FLASH_OFFSET, &fork, sizeof(fork));
}

static bool loaded(const ButtonLayers &expected) {
    return std::memcmp(&fork_storage.layers, &expected, sizeof(expected)) == 0;
}

static bool reset_layers() {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&fork_storage.layers);
    for (size_t i = 0; i < sizeof(ButtonLayers); i++) {
        if (bytes[i] != 0xFF) return false;
    }
    return true;
}

int main() {
    const ButtonLayers layers = sample_layers();
    ButtonLayers other = layers;
    other.layer[0].remap[0] = 5;

    // Upgrade: page 1 never written, layers still on the config page.
    erase();
    write_legacy_page(layers, true);
    fork_storage_load();
    CHECK(loaded(layers));
    CHECK(fork_storage.magic == FORK_STORAGE_MAGIC);

    // The old crc32 must match, or the bytes are not that layout.
    erase();
    write_legacy_page(layers, false);
    fork_storage_load();
    CHECK(reset_layers());

    // Page 0 from firmware without layers at that offset, such as upstream: its
    // crc32 covers fewer bytes, so a 0x4C at the old offset is not enough.
    erase();
    write_legacy_page(layers, true);
    uint32_t short_crc = crc32(flash + offsetof(Config, body), LEGACY_LAYERS_OFFSET - offsetof(Config, body));
    std::memcpy(flash + offsetof(Config, crc32), &short_crc, sizeof(short_crc));
    fork_storage_load();
    CHECK(reset_layers());

    // Fresh flash: nothing to load.
    erase();
    fork_storage_load();
    CHECK(reset_layers());
    CHECK(fork_storage.magic == FORK_STORAGE_MAGIC);

    // A valid page 1 wins over leftover layers on the config page.
    erase();
    write_legacy_page(layers, true);
    write_fork_page(other, true);
    fork_storage_load();
    CHECK(loaded(other));

    // A written but corrupt page 1 resets the layers. It does not fall back to
    // the config page, which may hold older layers.
    erase();
    write_legacy_page(layers, true);
    write_fork_page(other, false);
    fork_storage_load();
    CHECK(reset_layers());

    if (failures == 0) std::printf("fork_storage_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
