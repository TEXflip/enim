# Enim

Visualize enormous images in a small executable

## Quick Start

Built with CMake and Ninja. Needs a C++23 compiler with `#embed` (GCC 15+ or Clang 19+), and a GPU with OpenGL 3.3.

### Windows

```powershell
git submodule update --init
winget install NASM.NASM

cmake --preset win-x64 -G Ninja
cmake --build bin
.\bin\enim.exe path\to\image.bmp
```

### Linux

1. Needs X11/OpenGL development packages and GCC 15+. On Debian/Ubuntu:

   ```sh
   sudo apt install cmake ninja-build nasm libglx-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev \
       libxcursor-dev libxext-dev libgl1-mesa-dev
   git submodule update --init
   ```

   The Linux presets use `gcc-16`/`g++-16`. With another version, configure without a preset, e.g.
   `CC=gcc-15 CXX=g++-15 cmake -S . -B bin -G Ninja -DCMAKE_BUILD_TYPE=Release`.
2. Build and run:

   ```sh
   cmake --preset linux-x64 -G Ninja
   cmake --build bin
   ./bin/enim path/to/image.bmp
   ```

### macOS

```sh
brew install nasm          # Intel Macs only
brew install fast_float    # macOS <26 only
git submodule update --init
CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++ cmake --preset macos-x64 -G Ninja
cmake --build bin
./bin/enim path/to/image.bmp
```

## Supported formats

The format is detected from the file's signature, not its extension. Uncompressed images are memory-mapped and sent to the GPU as they are; compressed ones are decoded on the CPU and shown progressively, top to bottom.

| Format | Supported | Not supported yet |
|---|---|---|
| BMP | 24-bit (BGR) and 32-bit (BGRX) uncompressed; 32-bit bitfields with 8-bit channel masks, with or without alpha (RGBA, BGRA, ARGB, ...); top-down and bottom-up rows | palette (1/4/8-bit), 16-bit, RLE and embedded JPEG/PNG compression |
| QOI | RGB and RGBA, 8-bit | |
| JPEG | 8-bit grayscale and color (YCbCr/RGB), baseline, progressive, arithmetic and lossless | CMYK/YCCK, 12/16-bit, EXIF orientation |

## Test images

[tests/images/generate.py](tests/images/generate.py) writes test images for every supported pixel layout into `tests/images/out/`, using the Python environment of `pyproject.toml`:

```sh
uv run tests/images/generate.py           # add --force to regenerate existing files
```
