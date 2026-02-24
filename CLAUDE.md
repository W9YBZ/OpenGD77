# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenGD77 is open-source replacement firmware for DMR amateur radio transceivers based on the NXP MK22FN512VLL12 (ARM Cortex-M4). Supported targets: Radioddity GD-77, GD-77s, Baofeng DM-1801, Baofeng RD-5R.

## Build Commands

All builds run from `firmware/build/` using the Makefile in `firmware/`.

**Default (GD-77):**
```bash
cd firmware/build
make -f ../Makefile -j"$(nproc)"
```

**Other radio targets:**
```bash
make -f ../Makefile -j"$(nproc)" RADIO=GD77s   # -> OpenGD77S.sgl
make -f ../Makefile -j"$(nproc)" RADIO=DM1801  # -> OpenDM1801.sgl
make -f ../Makefile -j"$(nproc)" RADIO=RD5R    # -> OpenDM5R.sgl
```

**Clean build:**
```bash
make -f ../Makefile clean
```

**Output files** are in `firmware/build/bin/`: `.sgl` (flashable), `firmware.bin` (raw), `firmware.axf` (ELF with debug symbols), `firmware.map` (linker map).

## Prerequisites: Codec Binary Sections

The DMR codec is extracted from official Radioddity GD-77 v3.1.1 firmware due to licensing. The two binary files `firmware/linkerdata/codec_bin_section_1.bin` and `codec_bin_section_2.bin` must exist before building.

To generate them (one-time setup):
```bash
cd tools/codec_dat_files_creator
gcc -O2 -o codec_dat_files_creator codec_dat_files_creator.c

# Obtain GD-77_V3.1.1.sgl from official sources, then:
cd ../../firmware/linkerdata
../../tools/codec_dat_files_creator/codec_dat_files_creator /path/to/GD-77_V3.1.1.sgl
```

## No Test Suite

There is no automated test infrastructure. Testing is done on physical hardware.

## Architecture

The firmware runs on FreeRTOS with a single main application task (`mainTask()` in `source/main.c`). The codebase is organized into layers:

```
User Interface   source/user_interface/   Menu system, channel/VFO screens, settings menus
Functions        source/functions/        Business logic: codeplug, settings, TRX control, sound, VOX
Hardware         source/hardware/         Chip drivers: AT1846S (RF), HR-C6000 (DMR), UC1701 (OLED)
Interfaces       source/interfaces/       MCU peripherals: SPI, I2C, I2S, ADC, DAC, GPIO, clocks
DMR Codec        source/dmr_codec/        AMBE voice codec + binary sections from official firmware
Hotspot          source/hotspot/          DMR repeater/hotspot mode with full protocol stack
USB              source/usb/              Virtual COM port for CPS programming
RTOS             source/amazon-freertos/  FreeRTOS v9 kernel
```

**Platform conditionals:** Code uses `#ifdef PLATFORM_GD77`, `PLATFORM_GD77S`, `PLATFORM_DM1801`, `PLATFORM_RD5R` throughout to handle hardware differences. Set via `RADIO=` make variable.

## Key Files

| File | Purpose |
|------|---------|
| `source/main.c` | FreeRTOS init, `mainTask()` main loop, power management |
| `source/functions/trx.c` | RX/TX activation, frequency control, mode switching |
| `source/functions/codeplug.c` | Channel/zone/contact configuration from flash |
| `source/functions/settings.c` | User settings persistence |
| `source/functions/sound.c` | Audio pipeline, PTT tone, MDC1200, talk permit tone |
| `source/hardware/HR-C6000.c` | DMR chipset driver (largest hardware driver) |
| `source/user_interface/uiChannelMode.c` | Main channel-mode UI (largest UI file) |
| `firmware/linkerdata/` | Memory layout inputs; codec binaries live here |
| `firmware/Makefile` | Build system; defines compiler flags and targets |
| `firmware/linkerscripts/firmware_newlib.ld` | Linker script entry point |
| `firmware/linkerscripts/firmware_memory.ld` | Memory region definitions |

## Memory Layout

```
PROGRAM_FLASH  0x4000–0x7FBE0   ~504KB  Application code
  codec sec 1  @ 0x4400                 DMR codec part 1 (binary blob)
  codec sec 2  @ 0x50000               DMR codec part 2 (binary blob)
SRAM_UPPER     0x20000000  64KB         BSS + heap (1KB)
SRAM_LOWER     0x1FFF0000  64KB         Data + stack (1KB)
```

## Toolchain

Requires `arm-none-eabi-gcc` on `PATH`. The Makefile targets Cortex-M4 with hard-float ABI (`-mfpu=fpv4-sp-d16 -mfloat-abi=hard`), optimized for size (`-Os`), and links with `-nostdlib`.

The `tools/bin2sgl/` utility wraps the raw `.bin` into the `.sgl` format accepted by the Radioddity firmware loader. It is called automatically by the Makefile post-build.

Debug output uses Segger RTT (`source/SeggerRTT/`); `USE_SEGGER_RTT` is always defined.
