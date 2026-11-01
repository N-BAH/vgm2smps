# vgm2smps

**vgm2smps** is a tool that converts [VGM](https://en.wikipedia.org/wiki/VGM_(file_format)) files to [SMPS](https://segaretro.org/SMPS) (**S**ample **M**usic **P**layback **S**ystem) format, used by various games, but primarily used in *Sonic the Hedgehog (Sega Mega Drive/Genesis) romhacking*.

It may help you to port music music to your game, romhack or homebrew.

Project was designed as a generic VGM-to-tracker converter, but Flamewing's [SMPS2ASM](https://forums.sonicretro.org/threads/smps2asm-and-improved-s-k-driver.26876/) is a primary output format at this moment.

vgm2smps primirally tested on TheBlad768's [S.C.E.](https://github.com/TheBlad768/Sonic-Clean-Engine-S.C.E.-) with Clownacy's Sonic 2 Clone Driver V2.

## Features
- YM2612 and SN76489 chips support within SMPS opportunities.
- Human readable SMPS2ASM asm text output.
- Accurate pitch and volume effects conversion.
- DAC samples support, if it's optimized in VGM file.
- Loop support.
- Flexible conversion settings. You can specify channel masks and output length.
- YM2612's FMS imitation via SMPS modulation.
- DAC output from Datablock VGMs (it's a high quality emulator recorings) may produce obscure results 👀.

## Limitations
- Features are limited to SMPS limitations. SMPS does not support many YM2612 features natively as LFO, SSG-EG and Channel 3 Special Mode!
- DAC output still need additional human edit, but you can edit it in your favorite text editor and substitute your sample IDs.
- Result is still unoptimized by size. VGM is a container with a RAW chip command stream.

## Usage
- Download a release or build the project from source.
- Run vgm2smps in your terminal with `--help` argument and see usage info.
  ```
  vgms2msps is a tool that converts VGM files to SMPS format (Sample Music Playback System), used by various games.
  Allowed options:
    --help                           Print help message and exit
    --version                        Print project version and exit
    -i [ --input ] arg               Input file path (vgm/vgz)
    -o [ --output ] arg (=--)        Output path. If not specified stdout will be used
    -L [ --length ] arg              Reduce output length in frames
    --fm-mask arg (=111111)          Mask for enable YM2612 (FM) channels
    --psg-mask arg (=1111)           Mask for enable SN76489 (PSG) channels
    --disable-dac                    Disable DAC in output
    --smps-driver-version arg (=S3K) SMPS Driver version (S1, S2 or S3K)
  ```

- Select input file and output file, conversion settings.

### Usage examples:
Convert `1.vgm` file to `1.asm` with default settings: driver version is `S3K`, with DAC output and with full song's length.
```bash
vgm2smps -i 1.vgm -o 1.asm
```

Convert `some file.vgz` file to `1.asm` with `S1` driver version, disable DAC output and reduce oputput length to 3000 frames (3000 / 60 = 50 seconds):
```bash
vgm2smps -i "some file.vgz" -o 1.asm --smps-driver-version S1 -L 3000 --disable-dac
```

Convert `1.vgm` file to `1.asm` with default settings and channel FM mask: only FM1 and FM4 are enabled.
```bash
vgm2smps -i 1.vgm -o 1.asm --fm-mask 1001
```

## Building from source
For building vgm2smps from source you will need:
- C++ compiler with `C++20` support
- `CMake`
- Installed environment with libraries: `zlib`, `boost-container`, `boost-progam-options`, `spdlog`, `fmt`.
- Run typical cmake build procedure. (I will write a proper instruction later)

## Roadmap
- Output optimization by size.
- Create a user-friendly GUI for sample editing and fine-tuning the output.
- Create a good-looking website for the project.
