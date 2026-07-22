# CrossPoint Reader — M5Stack Paper v1.1

Open-source e-reader firmware for the **[M5Stack Paper v1.1](https://docs.m5stack.com/en/core/m5paper_v1.1)** — a 4.7", 960×540 e-ink development device.

This is a hardware port of [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) (originally built for the ESP32-C3 Xteink X3/X4) to the M5Stack Paper's classic-ESP32 hardware. It keeps CrossPoint's EPUB reading engine and reworks the platform layer — display, input, power, and file transfer — for the M5Paper.

> **Scope:** this fork targets the **M5Stack Paper v1.1 only**. The Xteink X3/X4 build (`default` env) is left intact and still compiles, but this project's focus, testing, and documentation are the M5Paper.

---

## The device

| | M5Stack Paper v1.1 |
|---|---|
| **MCU** | ESP32-D0WDQ6 (classic ESP32, dual-core Xtensa LX6 @ 240 MHz) |
| **RAM** | 512 KB internal + 8 MB PSRAM (framebuffer lives in PSRAM) |
| **Flash** | 16 MB |
| **Display** | 4.7" 960×540 e-ink, 16-level grayscale, IT8951E controller |
| **Input** | 3-position side wheel (up / press / down) + GT911 capacitive touch |
| **Storage** | microSD (shares the display SPI bus) |
| **Battery** | LiPo, sampled on GPIO35 |
| **USB** | USB-C via a CH9102 USB-to-UART bridge (serial only — see limitations) |

---

## Controls

The M5Paper has **no dedicated Back/menu buttons** — its only push control is the side wheel. The firmware maps everything onto it:

| Gesture | Action |
|---|---|
| **Roll wheel up / down** | Navigate lists · turn pages in the reader |
| **Short press** the wheel | **Confirm** / select |
| **Long press** the wheel (~0.65 s) | **Back** |
| **Double press** the wheel | **Sleep** |

The device also auto-sleeps after the configured inactivity timeout.

---

## Features

The reading experience is CrossPoint's, running on the M5Paper panel:

- **Reader engine** — EPUB 2/3 rendering, image handling, hyphenation, kerning, chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn, orientation control, focus reading, KOReader progress sync.
- **Formats** — `.epub`, `.txt`, `.bmp`, `.xtc/.xtch`.
- **Custom fonts** — drop `.cpfont` files on the SD card (see below), no reflash needed.
- **Library workflow** — folder browser, hidden-file toggle, long-press delete, recent books, SD-cache management.
- **Wireless file transfer** — WiFi + WebDAV for copying books onto the SD card, plus the web settings UI/API. WiFi is only powered on while the File Transfer screen is open, then shut off.
- **Customization** — themes, sleep-screen modes, status-bar controls, refresh cadence, and more.

---

## Getting your books onto the device

The M5Paper's USB port **cannot** present the SD card as a USB drive (see [Limitations](#known-limitations)). Use one of:

**A) Over WiFi (WebDAV)**
1. On the device: **Home → File Transfer**. It joins WiFi and shows the device's IP.
2. On your computer: connect to `http://<device-ip>/` as a WebDAV share.
   - **macOS Finder:** Go → Connect to Server (⌘K) → `http://<device-ip>/` → Connect as Guest.
3. Drag your EPUBs onto the mounted drive.

**B) SD card reader**

Pop the microSD out, copy files with a card reader, put it back. Simplest, no WiFi.

---

## Known limitations

These are specific to the M5Paper hardware/port and are honest about the current state:

- **Touch does not work yet.** The GT911 controller is detected over I²C but never enters scanning mode. On the classic ESP32 the touch reset line isn't on a controllable GPIO and the INT line (GPIO36) is input-only, so the usual GT911 reset/address sequence can't run. Navigation is fully covered by the wheel in the meantime; touch is under investigation.
- **No USB mass storage.** The classic ESP32 has no native USB — the USB-C port is a UART bridge only. Use WebDAV or an SD reader. (USB-drive emulation needs an ESP32-S2/S3.)
- **No over-the-air (OTA) updates.** Removed for this build — flash over USB (below).
- **Repurposed shared button:** hold-to-bookmark in the reader and the Power+Down screenshot combo are unavailable, because the wheel press is now Confirm/Back/Sleep.

---

## Build & flash

Everything is a normal [PlatformIO](https://platformio.org/) project. Developed and tested on an Apple-silicon Mac; Linux works the same way.

### Prerequisites

- PlatformIO Core (`pip install platformio`) or VS Code + the PlatformIO extension
- Python 3.8+
- A USB-C data cable

### Clone (with submodule)

```bash
git clone --recursive https://github.com/z88kat/crosspoint-reader
cd crosspoint-reader
# if you already cloned without --recursive:
git submodule update --init --recursive
```

### Build

```bash
pio run -e m5paper
```

### Flash

```bash
pio run -e m5paper -t upload
```

The build environment pins `upload_speed = 115200`. The M5Paper's CH9102/CH343 bridge is unreliable at higher rates (flashes time out mid-write), so a full flash takes a few minutes. If an upload fails partway with "chip stopped responding," just run it again — it's a bridge quirk, not a code problem. Set a higher `upload_speed` in a gitignored `platformio.local.ini` if your cable/host proves stable.

To target a specific port:

```bash
pio run -e m5paper -t upload --upload-port /dev/cu.usbserial-XXXXXXXX
```

Find the port with `ls /dev/cu.usbserial-*` (macOS) or `ls /dev/ttyUSB*` (Linux).

### Serial monitor / debugging

```bash
pio device monitor -e m5paper
```

Logs stream over UART0 at 115200 baud (the classic ESP32 has no USB-CDC, so logging uses the hardware UART).

---

## Internals

### Memory

Unlike the ESP32-C3 the original firmware was written for, the M5Paper has **8 MB of PSRAM**, so the ~63 KB 960×540 framebuffer is heap-allocated in PSRAM and internal DRAM stays comfortable. Free heap sits around 250 KB at the home screen. The SD-card caching described below is inherited from the C3 design and still used — it keeps chapter re-opens fast.

### SD-card cache

The first time a book's chapters are laid out, the result is cached to the SD card and reused on later opens, under `.crosspoint` on the card:

```text
.crosspoint/
├── epub_<hash>/         # one directory per book, named by content hash
│   ├── progress.bin     # reading position (chapter, page, etc.)
│   ├── cover.bmp        # generated cover image
│   ├── book.bin         # metadata: title, author, spine, TOC
│   ├── css_rules.cache  # parsed CSS rule cache
│   ├── img_*            # rendered image cache files
│   └── sections/        # per-chapter layout cache
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Deleting `/.crosspoint` clears all cached metadata and forces a full re-parse on next open. See [docs/file-formats.md](./docs/file-formats.md) for the on-disk formats.

### Custom SD-card fonts

Convert TTF/OTF files into `.cpfont` files that load from the SD card (no reflash):

1. Build them with the upstream tool at https://crosspointreader.com/fonts, or run `lib/EpdFont/scripts/fontconvert_sdcard.py` locally.
2. Copy them to the SD card under `/fonts/YourFont/` (or `/.fonts/YourFont/` to hide the folder).
3. Select the font in the device's font settings.

---

## Documentation

- [User Guide](./USER_GUIDE.md) — general reading features (written for X3/X4; controls differ on the M5Paper as noted above)
- [File formats](./docs/file-formats.md)
- [Project scope](./SCOPE.md)

---

## Credits

- Built on [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) — the open-source e-reader firmware this port is based on. All the reading-engine work is theirs.
- Hardware support (display / input / power drivers) uses the [**FreeInk SDK**](https://github.com/Free-Ink/freeink-sdk), which includes an M5Stack Paper board profile.
- Original inspiration: [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader).

Not affiliated with M5Stack, Xteink, or any device manufacturer.
