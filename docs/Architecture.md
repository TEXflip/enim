# Architecture

- [Code architecture](#code-architecture)
  - [Pipeline](#pipeline)
  - [Pixel layout (source side)](#pixel-layout-source-side)
  - [Display texture format](#display-texture-format)
    - [Direct upload (no conversion)](#direct-upload-no-conversion)
  - [Fast mode](#fast-mode)
    - [Strategies](#strategies)
    - [GPU conversion of raw bytes (`GpuConvert`)](#gpu-conversion-of-raw-bytes-gpuconvert)
    - [CPU conversion (`StreamUpload`)](#cpu-conversion-streamupload)
    - [GPU-decoded formats](#gpu-decoded-formats)
    - [Mipmaps](#mipmaps)
  - [Normal mode](#normal-mode)
    - [Pyramid file](#pyramid-file)
    - [Building](#building)
    - [Tiles](#tiles)
    - [`TiledRenderer`](#tiledrenderer)
  - [Image shader](#image-shader)
  - [Load plan](#load-plan)
  - [Formats](#formats)
    - [JPEG](#jpeg)
  - [Producer: `ImageSource`](#producer-imagesource)
  - [Destinations: `RowSink`s](#destinations-rowsinks)
  - [Threads](#threads)
  - [Coordinates](#coordinates)
  - [Directory layout](#directory-layout)
  - [Known limitations](#known-limitations)

The objective of the project is to read and show an image as fast as possible regardless of its format and size.

Format and size are the 2 variables determining how to read and render the image efficiently.
The size determines if the image is read in fast mode or normal mode:
- Fast mode is used for smaller images: the image must fit in a single OpenGL texture (`GL_MAX_TEXTURE_SIZE`) and must not exceed the fast mode memory budget (parameter settable in arguments or environment variable). The decoded image is copied directly into the GPU memory (if possible).
- Normal mode is used for larger images. The image pyramid is computed on the disk (and kept in a cache); the CPU samples the 256×256 pyramid tiles around the current view from it, and only the tiles the GPU doesn't hold yet are uploaded into a ring texture about the size of the screen.

After determining which mode to use, the next step is to choose the strategy on how to read the image efficiently based on its format:
- in Fast mode:
  - if the format is uncompressed: memory map the image and bind it directly to the GPU memory.
  - if it's encoded:
    - if the format can be decoded directly by the GPU (e.g., block-compressed formats): copy the encoded buffer into the GPU, a decode fragment shader runs once and writes the decoded image into an FBO, then rendering reads the decoded image (no decoding while rendering).
    - if the format cannot be decoded directly by the GPU: decode in chunks and upload each chunk to the GPU as it becomes available, the user will see the image progressively rendered.
- in Normal mode:
  1. if the image is encoded:
     - decode on disk the entire image (level 0 of the pyramid).
  2. compute the image pyramid on the disk.
  3. while is decoding (if compressed) or computing the pyramid, the CPU view sampler subsamples the image for rendering reading only random averaged pixels of the level 0 image, the user will see an artifacted preview of the image while the full resolution image is being prepared by tiles as they become available.

In both modes the final fragment shader only samples a texture, so rendering gets hardware filtering (and mipmaps in fast mode). The texture is display-ready: channel selection and bit depth conversion are done once, before rendering:
- in Fast mode, after decoding: by a conversion fragment shader (FBO to FBO) for GPU-decoded formats, or by the CPU for formats decoded on the CPU. Uncompressed images whose layout OpenGL can upload directly (`glTexImage2D`/`glTexSubImage2D`) need no conversion at all; the other uncompressed images are uploaded raw into a buffer texture and converted by a fragment shader into an FBO, whose texture is then used for rendering.
- in Normal mode, by the CPU view sampler while it produces the tiles.

The texture keeps the original bit depth whenever OpenGL has a filterable texture format for it (8 and 16 bit integer, 16 and 32 bit float), and only the channels that are displayed (MONO, MONO+ALPHA, RGB or RGBA).

# Code architecture

> Status: target design, partially built. Done: `PixelLayout`/`ImageInfo`, `ImageSource` + `RowSink` interfaces, `BmpSource` (24/32-bit, BI_RGB and byte-aligned bitfields, both row orders), `QoiSource` (CPU-decoded, streamed), `JpegSource` (libjpeg-turbo, 8-bit gray and RGB, streamed), `io/detect`, `DisplayParams`, `choose_texture_format` + direct upload rules, `make_plan`, `plan/loader`, `ImagePass`, and `FastRenderer` with the `DirectUpload`, `GpuConvert` and `StreamUpload` strategies (`RawBuffer`, `Framebuffer`, `ConvertPass` raw variant; swizzle; mipmaps built progressively by `MipmapBuilder` after loading). `StreamUpload` comes with `PixelConverter`, `UploadQueue`, `TextureUploadSink` and `core/wakeup`; the decode thread is owned by the renderer for now, not by a `Loader` session. Normal mode is done: `PyramidStore` (one cached `.pyr` file per image, levels 0…N as tiles), `PyramidBuilderSink`, `tile_selection`, `ViewSampler` (per-tile, with the preview fallback) and `TiledRenderer` (tile ring, stale ring), for both uncompressed and CPU-decoded sources. Debug flags: `--force-gpu-convert` and `--force-stream-upload` send uploadable BMPs through `GpuConvert` / `StreamUpload`, `--force-normal-mode` sends any image through normal mode. Profiling: `--profile` initializes a second plog instance (`kProfileLog`), and `PLOG_PROFILE << "..."` (in `logging.hpp`) writes a milestone to stderr with the time since startup and the thread id; without the flag the instance doesn't exist and the statement is skipped. The milestones are logged on the main thread only (`source opened`, `plan made`, `renderer init`, `first band uploaded`, `direct upload done` / `gpu convert done` / `stream upload done`, `mipmaps built`, `first tile uploaded`, `pyramid built`, `first frame`, `load finished`), so the time between two consecutive ones is a section of the load; `uv run tests/profile.py` loads every test image in every mode (normal mode cold and warm) and aggregates the sections over all runs. Next: `GpuDecode` (with `ConvertPass::init_texture`).

## Pipeline

```
 FAST MODE                                              (once, at load)               (every frame)
┌─────────────────────────────────────────────────────────────────────────────┐
│uncompressed, GL-uploadable layout ── mmap ── glTexSubImage2D ──────────────►│
│uncompressed, other layout ── mmap ── buffer texture ── convert pass (bands)►│ display texture  ──► image shader
│CPU-decoded ── decoder ── CPU convert ── glTexSubImage2D (progressive) ─────►│ (+ mipmaps, opt.)    (sample)
│GPU-decoded ── encoded upload ── decode pass ── [convert pass] ─────────────►│
└─────────────────────────────────────────────────────────────────────────────┘

 NORMAL MODE                                            (per view)                    (every frame)
 pyramid (.pyr mmap) ── ViewSampler: missing tiles only, copy level px,
                        channel select, depth convert ── glTexSubImage2D (tiles) ──► tile ring texture ──► image shader
```

- The **display texture** is the only thing the image shader reads. Its format comes from `choose_texture_format()` (below).
- The **image shader** is the current one: it maps the screen to texture coordinates with the `ViewTransform` and samples. Magnification uses `GL_NEAREST` (pixels stay sharp when zoomed in), minification uses `GL_LINEAR_MIPMAP_LINEAR` in fast mode (once the mip chain is built) and `GL_LINEAR` in normal mode.
- There is a single draw pass. It runs when the view changes or new data reaches the GPU. The decode and convert passes run once at load time and never while rendering.

On the CPU side, every loading strategy is the same pipeline with different pieces plugged in:

```
ImageSource (producer) ──rows──► RowSink(s) (destination) ──► UploadQueue ──► ImageRenderer (GPU)
        ▲                                                                           ▲
        └──────────────── chosen by make_plan() ────────────────────────────────────┘
```

- A **planner** turns (image info, format capabilities, GPU limits, settings) into a `LoadPlan`.
- An **`ImageSource`** decodes the image and emits rows in its native layout.
- One or more **`RowSink`s** decide where the rows go (GPU texture, level-0 file on disk, pyramid builder).
- The **renderer** only draws what has reached the GPU so far; the `Window` never knows which strategy is in use.

## Pixel layout (source side)

Sources, decoders and the pyramid work in the native layout; nothing is converted until the display texture is produced. The layout describes the storage exactly:

```cpp
// io/pixel_layout.hpp
enum class SampleType  : std::uint8_t { U8, U16, U32, I8, I16, I32, F16, F32 };
enum class RowOrder    : std::uint8_t { TopDown, BottomUp };
enum class ChannelRole : std::uint8_t { Gray, R, G, B, A, Unused, Other };

struct PixelLayout {
    SampleType sample;
    std::uint8_t channels;                       // as stored, may be > 4
    std::array<ChannelRole, kMaxChannels> roles; // header-declared
    std::size_t stride;                          // bytes per row, including padding
    RowOrder rows;
    bool big_endian;
    std::size_t bytes_per_pixel() const;
};

struct ImageInfo { std::size_t width, height; PixelLayout layout; };
```

Channel roles come from the file header, never from scanning the pixels. For example a 32-bit `BI_RGB` BMP is `{B, G, R, Unused}`.

## Display texture format

`choose_texture_format(layout, display)` picks the texture the image shader samples, and is the single place that knows the GL format rules:

```cpp
// render/gl/texture_format.hpp
struct TextureFormat {
    GLenum internal_format;          // GL_R8, GL_RG16, GL_RGB16F, GL_RGBA32F, ...
    std::uint8_t channels;           // 1..4: displayed channels only
    std::array<GLint, 4> swizzle;    // GL_TEXTURE_SWIZZLE_RGBA, e.g. {R, R, R, ONE} for mono
    SampleType sample;               // depth after conversion
};

TextureFormat choose_texture_format(const PixelLayout& layout, const DisplayParams& display);
```

**Channels:** only the displayed channels are stored. `Gray` becomes `R` with swizzle `{R, R, R, 1}`, gray+alpha becomes `RG` with swizzle `{R, R, R, G}`, then RGB and RGBA. The swizzle is texture state, so the shader never branches on it.

**Depth:** the native depth is kept when GL has a filterable format for it, otherwise it is converted:

| Source sample | Texture | Notes |
|---|---|---|
| U8 / U16 | `R8…RGBA8` / `R16…RGBA16` | normalized by the hardware |
| I8 / I16 | `R8_SNORM…` / `R16_SNORM…` | normalized by the hardware |
| F16 / F32 | `R16F…` / `R32F…` | filterable in GL 3.x |
| U32 / I32 | converted to `R32F…` | integer textures are not filterable; normalized with `range` during conversion |

Value range normalization (`DisplayParams::range`) for float and 32-bit data is applied during conversion. The image shader only samples.

### Direct upload (no conversion)

An uncompressed source is uploaded straight from the mmap (`glTexSubImage2D` into storage allocated beforehand, see Mipmaps) when GL can read its layout as is:
- channels map to a GL pixel format: `GL_RED`, `GL_RG`, `GL_RGB`/`GL_BGR`, `GL_RGBA`/`GL_BGRA`;
- the sample type maps to a GL type (`GL_UNSIGNED_BYTE`, `GL_UNSIGNED_SHORT`, `GL_HALF_FLOAT`, `GL_FLOAT`, ...) and `choose_texture_format` keeps it;
- big-endian data uses `GL_UNPACK_SWAP_BYTES`, row padding uses `GL_UNPACK_ALIGNMENT` or `GL_UNPACK_ROW_LENGTH`;
- bottom-up rows are handled by flipping the texture coordinates;
- channels that are not displayed are only allowed in the trailing position: uploading `GL_BGRA` data into an `RGB` internal format discards the 4th channel. The 32-bit `BI_RGB` BMP case is exactly this.

Anything else (more than 4 channels, `Unused` in the middle, U32, unsupported packing) goes through the GPU convert pass from a buffer texture (`GpuConvert`, below), still reading from the mmap.

## Fast mode

### Strategies

```cpp
enum class FastStrategy {
    DirectUpload, // uncompressed, GL-uploadable layout: glTexSubImage2D straight from the mmap
    GpuConvert,   // uncompressed, other layout: mmap bands → buffer texture → convert pass → FBO texture
    GpuDecode,    // encoded upload → decode pass → optional convert pass → texture
    StreamUpload, // CPU decoder → CPU convert → glTexSubImage2D, progressive
};
```

### GPU conversion of raw bytes (`GpuConvert`)

For uncompressed sources GL can't read directly, the raw bytes go to the GPU unchanged and a shader converts them:

1. The display texture is allocated with the display `TextureFormat` and attached to an FBO.
2. The source is processed in **bands of rows**. For each band:
   - its bytes are copied from the mmap into a staging `RawBuffer` with `glBufferSubData` (orphaning the buffer between bands to avoid stalls);
   - the convert shader's **raw-buffer variant** draws the band's rows into the FBO (viewport limited to the band). Each fragment reads its pixel's bytes with `fetch_bytes`, decodes the samples (`read_sample`: sample type, endianness, stride and row order come from uniforms), then applies channel selection, depth conversion and range.
3. When the last band is done, the staging buffer is freed and the FBO's texture is the display texture (the mip chain is built then).

The band size is the smaller of a staging budget (e.g. 64 MB) and `GL_MAX_TEXTURE_BUFFER_SIZE × 4` bytes, so the buffer limit never constrains the image size and the temporary memory stays small. The main thread draws between bands, so the image appears progressively. Changing the display parameters re-runs the bands from the mmap, which is cheap.

```cpp
// render/gl/raw_buffer.hpp
struct RawBuffer {                  // GL buffer + buffer texture view (GL_R32UI, usamplerBuffer)
    void allocate(std::size_t bytes);
    void upload(std::size_t offset, std::span<const std::uint8_t> data); // glBufferSubData
    GLuint texture() const;
};
```

`R32UI` rather than `R8UI`: the limit counts texels, so 4-byte texels give 4× the byte capacity. `shaders/include/raw_fetch.glsl` provides `fetch_bytes(addr, n)` (reads one or two words and shifts, so stride and sample offsets need no alignment) and `read_sample(addr, sample_type, big_endian) → float` (`uintBitsToFloat` for F32, manual half→float for F16, since GLSL 3.30 has no `unpackHalf2x16`).

### CPU conversion (`StreamUpload`)

For formats only the CPU can decode (today QOI and JPEG; later PNG, ...). The image appears progressively, top to bottom:

1. The display texture is created as for the other strategies (level 0 and mip storage allocated, render target set up while empty), then cleared to transparent so rows not decoded yet show the background.
2. A **decode thread** (`std::jthread`) runs `ImageSource::decode()` into a `TextureUploadSink`. For each chunk of rows (64 for QOI) the sink converts them with the `PixelConverter` into the texture's layout, on that thread, and pushes the band to the `UploadQueue`, then wakes the main thread (`wake_main_thread()` → `RGFW_stopCheckEvents`). Bottom-up sources are written reversed, so the texture is always top-down.
3. The **main thread** pops bands in `update()` and uploads them with `glTexSubImage2D`, up to 64 MB per frame. Between bands it waits for events, so no frames are drawn while the decoder works.
4. When the decoder has finished and every band is uploaded, the decode thread is joined and the mipmaps are built.

The queue is **bounded** (64 MB of converted rows): a decoder faster than the uploads waits in `push()`, so RAM use doesn't grow with the image. Stopping (closing the window during a decode) requests stop on the thread's `std::stop_token`: `push()` stops waiting, the decoder checks the token between chunks, and `~jthread` joins.

**Errors:** a decode error (e.g. truncated file) arrives through `on_done()`. The rows decoded before it stay visible, the rest stays transparent, and the error is logged. QOI emits the complete rows of the failing chunk before reporting the error.

```cpp
// pipeline/pixel_converter.hpp
/// Converts native rows into display texture rows on the CPU: channel selection, depth conversion, range.
struct PixelConverter {
    PixelConverter(const PixelLayout& src, std::size_t width, const TextureFormat& dst, const DisplayParams& display);
    std::size_t dst_row_bytes() const;
    // dst_stride < 0 writes the rows in reverse order (bottom-up sources)
    void convert_rows(const std::uint8_t* src, std::size_t src_stride, std::size_t count, std::uint8_t* dst, std::ptrdiff_t dst_stride) const;
};

// pipeline/upload_queue.hpp
struct UploadBand { std::size_t first_row, rows; std::vector<std::uint8_t> pixels; };
template <UploadItem Item>                             // any type with a `pixels` buffer (its size is counted)
struct UploadQueue {                                   // bounded MPSC queue, main thread pops
    bool push(Item item, std::stop_token stop);        // waits for room; false if stopped
    std::optional<Item> pop();
    void finish(std::expected<void, Error> result);    // end of stream
    bool pending() const;                              // items to upload, or a result not taken yet
    std::optional<std::expected<void, Error>> take_result();
};
using BandQueue = UploadQueue<UploadBand>;             // fast mode; normal mode uses UploadQueue<UploadTile>
```

- **`PixelConverter`:** the U8 source with the default range (every 8-bit format) is a channel shuffle. Everything else goes through a general per-sample path: read (any `SampleType`, byte swap), map `range` to [0, 1], write U8/U16 (clamped), F16 (float→half) or F32 (not clamped, for HDR). SNORM textures are replaced by UNORM for this strategy (`unsigned_normalized()`), like `GpuConvert`, since the range is mapped to [0, 1]. It is also meant for the `ViewSampler` (normal mode).
- **`loading()`** is true only while bands are queued (or the result wasn't taken yet), so the window loop sleeps between bands. The window checks `loading()` again after waiting, since the decode thread is what woke it.
- **Debug:** `--force-stream-upload` decodes uncompressed BMPs through `BmpSource::decode()` and this path, which exercises the converter on BGR/BGRX/BGRA/ARGB and bottom-up rows.

### GPU-decoded formats

Formats where every block can be decoded independently (block-compressed: BCn, ETC2, ASTC, ...):

1. **Upload the encoded data.**
   - Formats the hardware supports natively (BCn on desktop GL): `glCompressedTexImage2D`. The hardware decodes while sampling, so this is already a decoded texture.
   - Other block formats: the encoded blocks are uploaded into an integer texture with one texel per block (`RGBA32UI` for 16-byte blocks, `RG32UI` for 8-byte blocks). A per-codec **decode fragment shader** reads its block with `texelFetch` and writes the decoded pixels into an FBO the size of the image.
2. **Convert pass (if needed):** the convert shader's **texture variant** samples the decoded texture and writes into another FBO with the display `TextureFormat`, applying channel selection, depth conversion and range. It is skipped when the decoded texture is already display-ready, e.g. BC1/BC3/BC7 (RGBA8) or BC4 (mono, handled by swizzle).
3. Intermediate textures and the encoded upload are freed. The last texture is the display texture.

The decode and convert passes can't be merged when the decode is done by the hardware sampler, so they are kept separate. For shader-decoded codecs they could be fused later as an optimization.

```cpp
// render/gpu_passes/
struct DecodePass { std::expected<Texture, Error> run(const Texture& encoded, const ImageInfo& info, GpuCodec codec); };

/// One shader body (channel selection, depth conversion, range) with two input variants.
struct ConvertPass {
    // allocate the FBO texture (cleared to transparent) and build the input variant's program
    std::expected<void, Error> init_raw(const TextureFormat& dst, Vec2i size, const DisplayParams& display);     // GpuConvert
    std::expected<void, Error> init_texture(const TextureFormat& dst, Vec2i size, const DisplayParams& display); // GpuDecode (todo)
    void from_raw_band(const RawBuffer& band, const PixelLayout& src, std::size_t first_row, std::size_t rows);
    void from_texture(const Texture& decoded);
    const Texture& target() const;                                                    // drawn while bands arrive
    Texture finish();                                                                 // releases the FBO and the program
};
```

The FBO formats must be color-renderable. GL 3.3 does not guarantee that for 3-channel or SNORM formats, so `renderable()` turns RGB into RGBA and SNORM into UNORM of the same depth (the conversion maps `range` to [0, 1], so nothing is lost). The convert shaders are built from several sources: `#version`, the include files, then the pass body.

### Mipmaps

Mipmaps are **on**: once the display texture is complete, its chain is built progressively (below) and minification becomes trilinear (`GL_LINEAR_MIPMAP_LINEAR`). Until then, and for the levels not built yet, only the ready levels are sampled, so the image is shown immediately with bilinear minification. There is no key to toggle them: `FastRenderer::mipmaps` and `ImageRenderer::toggle_mipmaps()` (a no-op returning `false` in normal mode) remain, but nothing calls the toggle.

**Storage up front.** The display texture allocates levels 1…N (`Texture::allocate_mip_levels`, no data) when it is created, before level 0 gets its pixels. Adding levels later to a texture that already holds a large image makes the driver move it into new storage: a measured 70 ms stall for a 6000×6000 RGBA8 image, against under 2 ms per step when the storage already exists. The chain's VRAM (+⅓) is therefore always allocated, and it is already part of the fast mode budget.

**Render target up front.** For the same reason, the texture is attached to `MipmapBuilder`'s framebuffers (`FastRenderer::prepare_mip_builder`) while it is still empty. The first time a texture becomes a render target, the driver may re-lay out its whole storage. A `GpuConvert` texture is a render target from the start. A direct-upload texture is not, so before this setup, starting the mip build froze the window for about 40 ms on a 6000×6000 image (more on larger ones). `DirectUpload` therefore allocates level 0 empty, sets up the framebuffers, then uploads the pixels with `glTexSubImage2D`. The re-layout then happens on empty storage at load time: the upload of that image went from about 58 ms to 75 ms, and the mip build starts within about 2 ms.

**Progressive build.** The chain is filled on the main thread by `MipmapBuilder`, a bounded amount of work per frame, so events keep being handled while it is built:

- level *i* is a `glBlitFramebuffer` of level *i−1* at half size with `GL_LINEAR` (a 2×2 box filter for even sizes; for odd sizes the bilinear sampling skips some texels, like most `glGenerateMipmap` implementations). Read and draw framebuffers are attached to the two levels; blitting between different levels of one texture is allowed.
- each `update()` blits about 4 M destination texels (`kMipmapTexelsPerStep`, ~16 MB written at RGBA8). The level is always blitted whole and `glScissor` keeps the current band of rows, so the result is identical to a single blit, without seams. The small levels are done together in one step.
- `Texture::mip_levels_ready` counts the finished levels. `GL_TEXTURE_MAX_LEVEL` follows it, so trilinear filtering uses the part of the chain that is ready and never samples a level being written.
- `FastRenderer::loading()` is true while the chain is built, so the window loop polls instead of waiting and redraws after each step.

**During loading** the texture is incomplete, so `GL_TEXTURE_MAX_LEVEL` stays at 0; the build starts when the last band has arrived.

**Fallback.** Blitting needs a color-renderable format. `GpuConvert` textures always are (see `renderable()`); a direct-upload texture whose format isn't (e.g. SNORM, possibly RGB8 on some drivers) fails `MipmapBuilder::init` and the chain is generated at once with `glGenerateMipmap`, blocking.

Compressed textures will use the mipmaps stored in the file when present; otherwise the convert pass runs into an uncompressed texture so that mipmaps can be built.

## Normal mode

The GPU never sees the pyramid as a whole. The CPU produces the pyramid tiles around the current view, and only the tiles the GPU doesn't hold yet are uploaded, into a ring texture about the size of the screen.

### Pyramid file

One file per image in the cache directory: `<cache>/<key>.pyr`, holding **level 0 and every other level**.

- **Key:** MD5 of the image's file name followed by the bytes of its header (`ImageSource::header_bytes()`: BMP up to the pixel array, the 14-byte QOI header, JPEG from SOI to the end of the first SOS segment). Hashing the whole file would take seconds for the images normal mode is for; this takes microseconds, so the cache is looked up in `TiledRenderer::init()` on the main thread.
- **Cache directory:** `--cache-dir <dir>`, then `ENIM_CACHE_DIR`, then `%LOCALAPPDATA%/enim/cache` (Windows), `~/Library/Caches/enim` (macOS), `$XDG_CACHE_HOME/enim` or `~/.cache/enim` (Linux). Files are kept between runs and never evicted (yet).
- **Layout** (`pyramid/pyramid_format.hpp`): a 4 KiB header (magic, version, size, the source `PixelLayout`, tile size, level count, file size, `complete` flag), then levels 0…N down to 1×1, each half the previous one rounded up. Every level is stored as 256×256 tiles, tightly packed (edge tiles padded), row by row, top-down, in the source's channel order, sample type and byte order. So all levels share the source's `PixelLayout` with `stride = 256 × bytes per pixel`, and one `PixelConverter` serves them all. Level 0 is copied into the file for uncompressed sources too, so the cache is self-contained.
- **Creation:** the free space is checked first (clear error if the disk is too full), the file is allocated at its final size (on Windows marked sparse first, so that writing far into it doesn't zero-fill the gap) and memory-mapped read-write. `PyramidStore` owns the mapping; the OS page cache is the tile cache.
- **Reuse:** a file whose header matches the image and has `complete` set is used as is: nothing is decoded. Anything else is rebuilt. `complete` is written last, after flushing, so an interrupted or failed build is never mistaken for a complete one. Closing the window during a build deletes the incomplete file.

### Building

`TiledRenderer` owns one build thread (`std::jthread`), started only when the pyramid isn't complete. It runs `ImageSource::decode()` into a `PyramidBuilderSink`, for uncompressed sources too (`BmpSource::decode()` streams rows out of the mmap). Decoding and building are a single pass, and level 0 is never read back.

- The sink writes each row into the level-0 tiles and cascades 2× box downsampling: each level keeps one row waiting for its partner (row `y` pairs with `y ^ 1`, top-down); a row whose partner is outside the level (odd height) is averaged alone. Horizontal pairs are averaged the same way, in float (integer arithmetic for U8), and stored back in the native type.
- Rows arrive in storage order, so bottom-up sources produce every level bottom row first, and each level's written rows are always a prefix (top-down) or a suffix (bottom-up) of it. **Readiness** is therefore one atomic row counter per level (`PyramidStore::row_ready(level, y)`), and a tile row becomes sharp as soon as its own row is written, at any level. A global `progress` counter changes whenever rows are written.
- The sink wakes the main thread at most every 100 ms, and when it is done. The decode thread checks the stop token between chunks, so closing the window stops it within one chunk.

Measured on a fast SSD: a 30000×22300 BMP (2 GB) builds in 19 s, a 40869×30379 JPEG (538 MB) in 54 s (decoding dominates). The second run opens instantly.

### Tiles

The unit of work is a **pyramid tile**: 256×256 pixels of one level, aligned like the tiles of the `.pyr` (tile (tx, ty) of level l+1 covers tiles (2tx…2tx+1, 2ty…2ty+1) of level l). One texel per pixel of the chosen level, not per screen pixel: the CPU only copies and converts; the GPU does the remaining 1–2× minification with `GL_LINEAR`, or the magnification with `GL_NEAREST`.

- **Level selection:** `level = clamp(floor(log2(1 / zoom)), 0, max_level)`, so one screen pixel covers between 1 and 2 pixels of the level (less than 1 when zoomed in past 100% at level 0).
- **Tile selection** (`pyramid/tile_selection`, pure):
  - `ring_slots(window, level)`: the ring's size in tiles per axis, `ceil(2 × window / 256) + 3` (a level pixel can be half a screen pixel, plus one tile of misalignment and a one-tile margin on each side), clamped to the level's tiles and to `GL_MAX_TEXTURE_SIZE`. It depends only on the window size, so zooming within a level keeps the ring.
  - `tile_window(view, level, slots)`: the tiles touching the visible pixels, grown by one tile on each side, clamped to the level and then to the slots (centered on the visible part).
  - `tile_pixels(level, tx, ty)`: the tile's pixels, clamped to the level (edge tiles are smaller).
- **`ViewSampler`** (`pyramid/view_sampler`): a dispatcher thread with a single-slot mailbox (a newer `TileRequest` replaces the waiting one) runs the request's tiles, nearest to the view's center first, over a `ThreadPool` (one worker per hardware thread, minus one). When a newer request arrives, the tiles not started yet are skipped: the renderer always sends again the tiles it still waits for. Each tile row is produced by the first source that has it:
  1. the row of the tile's level, converted (`PixelConverter::convert_span`);
  2. at level 0 of an uncompressed source, the row of the source mmap itself;
  3. the nearest coarser level that has the row, upsampled (nearest);
  4. the **preview**: for each texel, random samples of the level-0 pixels it covers, averaged. They come from the source mmap (uncompressed sources: available at once) or from the level-0 rows already decoded. The samples are fixed per texel, and each refinement pass doubles them (1, 2, 4, 8, 16);
  5. nothing: the texel stays transparent.

  Each tile goes to the `TileQueue` (`UploadQueue<UploadTile>`) as one `UploadTile`: level, the `TileJob` (tile and pass), width × height, and `complete` if no fallback was used.
- **Ring texture:** the frame `TextureFormat` is the display format made UNORM (the converter maps `range` to [0, 1]) **with an alpha channel** (`with_alpha`: mono becomes gray+alpha, RGB becomes RGBA), so that texels not sampled yet are really transparent. Colors are **premultiplied** by alpha (the converter multiplies them when the alpha comes from the source; opaque images are unchanged) and rings are blended with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`: with straight alpha, `GL_LINEAR` would darken the edge between sampled and transparent texels. No mipmaps.

```cpp
struct TileJob { std::size_t tx, ty; int pass; };    // pass: preview refinement
struct TileRequest {
    std::uint64_t generation;    // newer requests cancel the tiles of older ones not started yet
    int level;
    std::vector<TileJob> tiles;  // nearest to the view's center first
};
```

### `TiledRenderer`

- **Tile ring:** one texture of `slots.x × slots.y` tile slots for the current level. Tile (tx, ty) always lives in slot (tx mod slots.x, ty mod slots.y), so the window can move without moving any texel: GL 3.3 can't copy within a texture cheaply, and it doesn't need to. The texture wraps with `GL_REPEAT` and neighbouring tiles of the window are neighbouring slots modulo the ring, so filtering across tile seams is correct.
- **Slots:** each slot records the tile it holds, the pass last requested, the pass uploaded (−1: transparent), whether it is complete, and the pyramid progress when it was requested. `update()`:
  1. uploads queued tiles (up to 64 MB) with `glTexSubImage2D` at their slot, if the slot still holds that tile of the current level (others are dropped);
  2. starts a new ring when the level or the ring size changes;
  3. assigns the slots to the tiles of the new `tile_window`: a slot that held another tile is cleared to transparent, so it never shows a wrong tile;
  4. posts a `TileRequest` with the tiles that are **missing**, or **due for refinement** (incomplete, and either more preview passes remain, or the pyramid progressed since the request, at most every 100 ms while building). The tiles still being sampled go with them, since the new request cancels the old one. Nothing is posted when nothing is missing or due, so panning within the margin, or a static view, costs nothing; a pan past it requests only the newly exposed column or row of tiles.
- **Stale ring:** after a level change the old ring is kept as `previous` (unless the new one already showed something and the old one nothing) and drawn under the new one at its own level, until every visible tile of the new ring has texels. Its texture is reused for the next ring when the size matches.
- **Drawing:** `ImagePass::draw_ring` maps image coordinates to texels of the level, clamps them to the window shrunk by half a texel (like `GL_CLAMP_TO_EDGE`, so linear filtering never reads a wrapped-around slot at the window's or the image's edge), and discards fragments outside the window.
- **`loading()`** is true only while there is work for `update()`: queued tiles, a finished build to join, or a tile whose refinement is due. Otherwise the window waits for events; the sampler and the builder wake it.
- **Destruction:** the build thread is stopped and joined, then the sampler, then the store is unmapped (and the incomplete file deleted).

## Image shader

One program for both modes: a quad covering the window, texture coordinates from the `ViewTransform`, and a texture sample. The `region_min/max` uniforms give the image region the texture covers (the whole image in fast mode, the ring's tile window in normal mode); fragments outside the region or the image are discarded. Image coordinates become texel coordinates with `texel_scale/offset`, clamped to `texel_min/max` (unbounded in fast mode, the window minus half a texel for a ring), then divided by the texture size. Background outside the region is the clear color. Alpha is composited over the background with standard blending.

`DisplayParams` never reach the shader:

```cpp
// view/display_params.hpp
struct DisplayParams {
    std::array<std::int8_t, 4> channel_map; // source channel per RGBA output, -1 = constant
    std::array<float, 4> constant{0, 0, 0, 1};
    Vec2f range;                            // value → [0, 1]; default from SampleType (255, 65535, 1.0, ...)
    // later: per-channel range, gamma, colormap, ...
};
```

Defaults come from the channel roles: `Gray` is replicated (swizzle), BGR is handled by the upload format or the converters, `A` is kept, `Unused` is dropped.

| Changing | Fast mode | Normal mode |
|---|---|---|
| a reorder of the stored channels | new swizzle, instant | new tiles |
| anything else (other channels, `range`) | re-run conversion from the source: GPU convert bands from the mmap, convert pass from the decoded data, or a new decode for `StreamUpload` | new tiles |

## Load plan

The decision table at the top is a pure function, easy to unit-test:

```cpp
enum class Mode { Fast, Normal };

enum class Level0Source {
    SourceMmap,   // uncompressed: the source file already is level 0, no copy
    DecodeToDisk, // encoded: level 0 exists only once decoded into the pyramid file
};

struct LoadPlan {
    Mode mode;
    FastStrategy fast;      // valid if mode == Fast
    Level0Source level0;    // valid if mode == Normal
    TextureFormat texture;  // display texture (fast) or tile ring texture (normal)
};

LoadPlan make_plan(const ImageInfo& info, const FormatCaps& caps, const GpuLimits& gpu, const Settings& s);
```

Mode selection:

```cpp
TextureFormat tex = choose_texture_format(info.layout, default_display(info.layout));
std::size_t tex_bytes = info.width * info.height * tex.bytes_per_pixel() * 4 / 3;  // + mip chain
bool fast = info.width  <= gpu.max_texture_size
         && info.height <= gpu.max_texture_size
         && tex_bytes + transient_bytes(plan) <= settings.fast_mode_max_bytes;
// transient_bytes: GpuDecode keeps encoded + decoded + converted textures alive during the passes,
//                  GpuConvert only the staging band buffer
if (settings.force_fast_mode)   fast = true;
if (settings.force_normal_mode) fast = false;
```

`fast_mode_max_bytes` comes from `--fast-max-mem <size>` (accepts `512M`, `2G`), then the `ENIM_FAST_MAX_MEM` environment variable, then a built-in default.

Format capabilities:

```cpp
struct FormatCaps {
    bool raw_mappable;   // pixels are stored uncompressed and can be read straight from the mmap
    bool gpu_decodable;  // block-compressed: native GL support or a decode shader exists
    bool streamable;     // can decode top-to-bottom in row chunks
};
```

Non-streamable formats still use `StreamUpload`: they emit a single chunk at the end. Their temporary CPU buffer is bounded by the fast mode budget.

## Formats

| Format | Strategy | Decoder | Status |
|---|---|---|---|
| BMP | `DirectUpload` (`GpuConvert` for layouts GL can't read) | own (`BmpSource`) | done |
| QOI | `StreamUpload` | own (`QoiSource`) | done |
| JPEG | `StreamUpload` | libjpeg-turbo (`JpegSource`) | done: 8-bit gray and color; not yet CMYK/YCCK, 12/16-bit |

### JPEG

- **Strategy:** `StreamUpload`. JPEG can't use `GpuDecode`: its 8×8 blocks are independent only after entropy decoding, and the Huffman bitstream is sequential (a block's bits start where the previous one's end, and DC coefficients are delta-coded). Restart markers would allow independent segments, but many files have none. OpenGL has no hardware JPEG decode.
- **Decoder:** libjpeg-turbo, through the libjpeg API (`jpeg_read_scanlines`), not TurboJPEG (which only decodes the whole image at once). SIMD Huffman, IDCT and color conversion; covers baseline, progressive, arithmetic, lossless, 12-bit and CMYK files. It is a git submodule in `third_party/libjpeg-turbo` (pinned to a release tag), built with `ExternalProject_Add` (it refuses `add_subdirectory`) in Release, and linked statically. Its x86 SIMD code is NASM assembly: the build requires NASM (`REQUIRE_SIMD`) instead of silently falling back to the slower C code.
- **`JpegSource`:**
  - `open()` reads the header: Gray, or RGB with `out_color_space = JCS_EXT_RGB` (from YCbCr or RGB), U8, top-down. CMYK/YCCK (libjpeg can't convert them to RGB) and 12/16-bit images are rejected here with a message. The file is memory-mapped and read with `jpeg_mem_src`.
  - `caps()`: streamable; not raw-mappable, not GPU-decodable.
  - `decode()` emits bands of 64 rows (a multiple of every MCU height) and checks the `stop_token` between bands. Progressive (multi-scan) files are read whole by `jpeg_start_decompress` before the first row comes out, then emitted the same way.
  - Errors: libjpeg's `error_exit` is replaced by a handler that `longjmp`s back to a `setjmp` in `open()`/`decode()`, which return `std::expected`. Since a `longjmp` skips destructors, every C++ object of those functions is created before the `setjmp`. As with QOI, the complete rows of the failing band are emitted first.
  - Truncated files: libjpeg only warns (`JWRN_JPEG_EOF`) and fills the missing data. For single-scan files the warning is made fatal, so the rows not decoded stay transparent. Multi-scan files have every block from the first scans, so they are shown whole with less detail and the error is reported after the last row. Other warnings (corrupt data) are logged once and decoding goes on.
- **Later:**
  - EXIF orientation.
  - Normal mode: libjpeg-turbo's scaled decoding (`scale_num/scale_denom` = 1/2, 1/4, 1/8, a partial IDCT) as a fast preview or coarse pyramid level, and `jpeg_skip_scanlines` / `jpeg_crop_scanline`.


## Producer: `ImageSource`

Replaces the current `FileReader`. One decode entry point serves every strategy:

```cpp
/// Receives decoded rows in the source's native PixelLayout.
struct RowSink {
    virtual ~RowSink() = default;
    virtual void on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) = 0;
    virtual void on_done(std::expected<void, Error> result) = 0;

    /// Optional: a buffer for the full image the decoder can write into directly. Empty if unsupported.
    virtual std::span<std::uint8_t> full_image_buffer() { return {}; }
};

struct ImageSource {
    virtual ~ImageSource() = default;
    virtual std::expected<void, Error> open(const std::filesystem::path&) = 0;
    virtual const ImageInfo& info() const = 0;
    virtual FormatCaps caps() const = 0;

    virtual std::span<const std::uint8_t> mapped_pixels() { return {}; } // raw_mappable only
    virtual std::span<const std::uint8_t> encoded_bytes() { return {}; } // gpu_decodable only
    virtual std::expected<void, Error> decode(RowSink& sink, std::stop_token stop) = 0;
};
```

- Streamable decoders call `on_rows` many times.
- Non-streamable decoders either write into `full_image_buffer()` when the sink offers one, or call `on_rows` once with the whole image.

## Destinations: `RowSink`s

| Sink | Used by | Does |
|---|---|---|
| `TextureUploadSink` | Fast: StreamUpload | Converts rows with the `PixelConverter` and queues them for the main thread, which uploads them with `glTexSubImage2D`. |
| `PyramidBuilderSink` | Normal | Writes rows into level 0 of the pyramid file and cascades 2× downsampling into levels 1…N while level 0 is still being produced. |

Decoding and pyramid building happen in a single pass: `PyramidBuilderSink` writes level 0 into the pyramid file itself, coarse levels become available while level 0 is produced, and level 0 is never read back from disk. `Level0Source` only decides where the preview reads level 0 before it is in the pyramid: the source mmap (`SourceMmap`), or nowhere (`DecodeToDisk`).

## Threads

```
 decode thread       ImageSource::decode ──► RowSink (TextureUploadSink / PyramidBuilderSink ──► .pyr mmap)
 view sampler        TileRequest ──► ThreadPool: pyramid / source mmap ──► converted tiles ──► UploadQueue
                                                  │
 main thread (GL)    UploadQueue.pop() up to 64 MB per wake-up ──► glTexSubImage2D ──► image shader
                     DirectUpload: glTexSubImage2D from the mmap
                     GpuConvert: mmap band ──► RawBuffer ──► convert pass ──► FBO texture (band by band, once)
                     GpuDecode: encoded upload ──► decode pass ──► convert pass (once)
                     mipmaps: MipmapBuilder, ~4 M texels of level blits per wake-up
                     ▲ woken by RGFW_stopCheckEvents() whenever something is queued
```

All GL calls stay on the main thread; workers only hand over CPU buffers. `GpuConvert` needs no worker: the mmap is already readable, so the main thread uploads and converts one band per wake-up and redraws in between. A single `UploadQueue` template serves every mode: `UploadQueue<UploadBand>` (rows) in fast mode, `UploadQueue<UploadTile>` in normal mode. For `StreamUpload` the decode thread is a `std::jthread` owned by `FastRenderer` (a `StreamJob` holding the converter, the queue, the sink and the thread, destroyed thread first). In normal mode `TiledRenderer` owns the build thread and the `ViewSampler` (its dispatcher thread and `ThreadPool`); `update(view)` posts a new `TileRequest` when the view needs tiles the ring doesn't hold (pan out of the margin, zoom to another level, window resize), or to refine incomplete tiles; tiles whose slot holds another tile by the time they arrive are dropped.

## Coordinates

`ViewTransform` is the shared camera. Only `Window` mutates it (pan, zoom, fit, resize); renderers receive it as `const&` each frame, and the normal mode renderer turns it into the tiles of each `TileRequest`. All positions are in level-0 image pixels, top-down; conversion to pyramid levels happens inside the view sampler. Bottom-up sources are flipped through the texture coordinates (fast mode direct upload), by the convert shader (`GpuConvert`), or by the CPU converter / pyramid builder (everything else). `image_size` is set once from `renderer->image_size()` after loading.

## Directory layout

```
src/
  main.cpp
  core/        errors, logging, linalgebra, settings/ (+ fast_mode_max_bytes)
               thread_queue.hpp    MPSC queue
               thread_pool.*       view sampler bands
               wakeup.*            wraps RGFW_stopCheckEvents
               cache_dir.*         cache directory, key = MD5(file name + header bytes), <key>.pyr
               md5.*               MD5 for the cache key
  app/         window, events, rgfw   (currently window/)
  view/        view_transform.hpp, display_params.hpp
  io/
    detect.*             signatures + detect_format
    pixel_layout.hpp     SampleType, PixelLayout, ChannelRole, ImageInfo
    image_source.hpp     ImageSource, FormatCaps
    row_sink.hpp
    formats/bmp/, qoi/   one folder per decoder
  plan/
    load_plan.*          make_plan(): mode + strategy + texture format, pure
    loader.*             wires source → sinks → renderer from a plan, owns the threads
  pipeline/
    upload_queue.hpp     converted bands waiting for the main thread
    pixel_converter.*    native rows / spans → display texture rows (StreamUpload, ViewSampler)
    sample_io.hpp        read/write one sample of any SampleType (converter, pyramid builder)
    texture_upload_sink.*
  pyramid/
    pyramid_format.hpp   .pyr header (with PixelLayout), level geometry, tile addressing
    pyramid_builder.*    PyramidBuilderSink: level 0 + native-type 2x downsampling, a RowSink
    pyramid_store.*      read-write mmap of the .pyr, per-level row readiness, progress
    tile_selection.*     ViewTransform → level, ring size, tile window, tile pixels, pure
    view_sampler.*       TileRequest → one band per tile (copy, convert, coarser level / preview fallback)
  render/
    image_renderer.hpp   init / update / draw / loading / image_size (the window keeps redrawing while loading())
    gl/                  program utils, debug helpers, texture.*, framebuffer.*, raw_buffer.*, mipmap_builder.*
                         texture_format.*  choose_texture_format, GL upload params (format/type/unpack)
    image_pass.*         textured quad + ViewTransform → screen
    gpu_passes/
      codecs.*           GpuCodec → native GL format or decode shader
      decode_pass.*      encoded blocks → decoded texture (FBO)
      convert_pass.*     decoded texture or raw band → display texture (FBO)
    fast_renderer.*      owns the display texture, picks the upload path, drives GpuConvert bands
    tiled_renderer.*     tile ring slots, posts the missing tiles, uploads them, draws the current/stale ring
    shaders/
      image.vert/.frag      final: sample the texture (today's main.vert/.frag)
      fullscreen.vert       for the decode and convert passes
      decode/bc*.frag ...   one decode shader per shader-decoded codec
      include/raw_fetch.glsl  fetch_bytes, read_sample, half→float
      include/convert.glsl  channel selection, depth conversion, range
      convert_tex.frag      convert pass, decoded texture input
      convert_raw.frag      convert pass, raw buffer texture input
tests/
  images/generate.py     test images for every pixel layout (uv run tests/images/generate.py)
```

`FastRenderer` and `TiledRenderer` both draw through `ImagePass`. The current `dispatcher` splits into `io/detect` (which format is this?) and `plan/loader` (what do we do with it?).

A load then reads:

```cpp
auto source = open_source(path);                                    // io/detect + factory
LoadPlan plan = make_plan(source->info(), source->caps(), gpu, settings);
auto session = Loader::start(std::move(source), plan, upload_queue, wakeup);
window.set_renderer(session->renderer());                           // session owns threads, sinks, renderer
```

## Known limitations

- **Display changes in fast mode:** the texture only holds the displayed channels, already converted. Reordering stored channels is an instant swizzle change; anything else (another channel of a >4-channel image, a different `range`) re-runs the conversion from the source, which for `StreamUpload` means decoding again.
- **Mipmap memory:** the mip chain adds about a third of the texture size. Its storage is allocated with the texture, before the image data (to avoid relocating the image), and it is included in the fast mode budget.
- **RGB textures:** many drivers store `RGB8` internally as 4 bytes per pixel, so RGB saves less VRAM than expected (RAM and upload bandwidth savings are real). FBO targets for 3 channels are RGBA anyway, since RGB is not guaranteed renderable.
- **Integer precision:** U32/I32 data is converted to F32, so values above 2²⁴ lose precision. Irrelevant for display; exact values (e.g. a pixel inspector) must be read on the CPU side.
- **Normal mode latency:** panning past the ring's one-tile margin needs the newly exposed tiles (only those) from the CPU; zooming to another level or changing the display needs every tile of the new ring. The stale ring hides a level change, but newly exposed tiles of a pan stay empty until they arrive.
- **Normal mode cache key:** the key is the file name and header, not the pixels: editing an image's pixels without changing its name or header (same size and format) reuses the old pyramid. Delete the `.pyr` (or use another `--cache-dir`) to rebuild it. Pyramids are never evicted: each one takes about 4/3 of the decoded image size on disk.
- **Normal mode disk usage while building:** the whole `.pyr` is allocated up front (sparse on Windows); the build fails at the start with a clear message if the disk doesn't have room for it.
- **GPU-decoded peak memory:** encoded, decoded and converted textures can coexist during the passes; `transient_bytes` accounts for it in the budget.
- **Rows not loaded yet with RGB textures (fast mode):** the texture is cleared to transparent, but an RGB texture's swizzle forces alpha to 1, so missing rows show black instead of the background color (`StreamUpload` and `GpuConvert` while loading, a truncated file afterwards). Normal mode rings always have an alpha channel, so they don't have this problem.
- **`GpuConvert` and 3 channels:** the convert pass renders into an FBO, so 3-channel results are stored as RGBA (RGB is not guaranteed renderable). Direct upload keeps true RGB textures.
