# Architecture

The objective of the project is to read and show an image as fast as possible regardless of its format and size.

Format and size are the 2 variables determining how to read and render the image efficiently.
The size determines if the image is read in fast mode or normal mode:
- Fast mode is used for smaller images: the image must fit in a single OpenGL texture (`GL_MAX_TEXTURE_SIZE`) and must not exceed the fast mode memory budget (parameter settable in arguments or environment variable). The decoded image is copied directly into the GPU memory (if possible).
- Normal mode is used for larger images. The image pyramid is computed in the disk; the CPU samples the current view from it and only a screen-sized frame is uploaded into the GPU memory on runtime.

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
- in Normal mode, by the CPU view sampler while it produces the screen-sized frame.

The texture keeps the original bit depth whenever OpenGL has a filterable texture format for it (8 and 16 bit integer, 16 and 32 bit float), and only the channels that are displayed (MONO, MONO+ALPHA, RGB or RGBA).

# Code architecture

> Status: target design, partially built. Done: `PixelLayout`/`ImageInfo`, `ImageSource` + `RowSink` interfaces, `BmpSource` (24/32-bit, BI_RGB and byte-aligned bitfields, both row orders), `QoiSource` (CPU-decoded, streamed), `JpegSource` (libjpeg-turbo, 8-bit gray and RGB, streamed), `io/detect`, `DisplayParams`, `choose_texture_format` + direct upload rules, `make_plan`, `plan/loader`, `ImagePass`, and `FastRenderer` with the `DirectUpload`, `GpuConvert` and `StreamUpload` strategies (`RawBuffer`, `Framebuffer`, `ConvertPass` raw variant; swizzle; mipmaps built progressively by `MipmapBuilder` after loading). `StreamUpload` comes with `PixelConverter`, `UploadQueue`, `TextureUploadSink` and `core/wakeup`; the decode thread is owned by the renderer for now, not by a `Loader` session. Debug flags: `--force-gpu-convert` and `--force-stream-upload` send uploadable BMPs through `GpuConvert` / `StreamUpload`. Next: `GpuDecode` (with `ConvertPass::init_texture`), then normal mode. Normal mode currently falls back to fast mode when the image fits in a texture.

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
 pyramid (mmap) ── ViewSampler: level select, resample,
                   channel select, depth convert ── glTexSubImage2D (bands) ──► frame texture ──► image shader
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
struct UploadQueue {                                   // bounded MPSC queue, main thread pops
    bool push(UploadBand band, std::stop_token stop);  // waits for room; false if stopped
    std::optional<UploadBand> pop();
    void finish(std::expected<void, Error> result);    // end of stream
    bool pending() const;                              // bands to upload, or a result not taken yet
    std::optional<std::expected<void, Error>> take_result();
};
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

The GPU never sees the pyramid. The CPU turns the current view into one screen-sized frame:

- **Pyramid:** built on the CPU in the native sample type (average in float/double, store back in the native type). Tiles are 256×256, tightly packed (stride = 256 × bytes per pixel), top-down, in the source channel order. The on-disk header stores the `PixelLayout`. `PyramidStore` mmaps it; the OS page cache acts as the tile cache.
- **Level selection:** `level = clamp(floor(log2(1 / zoom)), 0, max_level)`, so one screen pixel covers between 1 and 2 pixels of the chosen level (or less than 1 when zoomed in past 100% at level 0).
- **`ViewSampler`** (CPU worker): given a `FrameRequest`, it produces a `Frame`:
  1. computes the level and the tiles covering the view (`tile_selection`, pure);
  2. for each screen pixel, reads the nearest pixel or a 2×2 average from that level;
  3. converts it with the `PixelConverter` into the frame `TextureFormat` (same rules as fast mode).

  Rows are independent, so the work splits across a thread pool by bands of rows. A frame is emitted band by band and uploaded with `glTexSubImage2D` as bands arrive.
- **Fallback while the pyramid is not ready:** if a tile of the chosen level doesn't exist yet, the sampler reads the nearest coarser level that exists. If no level exists yet, it reads random averaged samples of level 0 directly from the level-0 mmap (the source file itself, or the decoded file while it is being written). This is the artifacted preview, refined with more samples on each pass. With `Level0Source::SourceMmap` the preview is available immediately.

```cpp
struct FrameRequest {
    std::uint64_t generation;    // newer requests cancel older ones
    ViewTransform view;          // snapshot
    DisplayParams display;       // channel selection and range applied on the CPU
    Vec2i size;                  // framebuffer size
};

struct Frame {
    std::uint64_t generation;
    Vec2f image_min, image_max;  // image region the frame covers (level-0 px)
    Vec2i size;
    TextureFormat format;
    std::vector<std::uint8_t> pixels;
    bool complete;               // false while the preview is still refining or tiles are missing
};
```

- **GPU side:** one frame texture, reallocated on window resize: about 8 MB for a 1920×1080 RGBA8 frame, whatever the image size. No mipmaps: the frame is 1:1 with the screen.
- **Stale frames:** CPU sampling takes some time after each pan or zoom. Meanwhile the renderer keeps drawing the last frame at its `image_min/max` under the new view: it slides and scales with the camera (with hardware filtering), and areas it doesn't cover show the background. The new frame replaces it when it arrives.

## Image shader

One program for both modes, as today: a quad covering the texture's image region (`image_min/max` in level-0 px: the whole image in fast mode, the frame region in normal mode), texture coordinates from the `ViewTransform`, and a texture sample. Background outside the region is the clear color. Alpha is composited over the background with standard blending.

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
| a reorder of the stored channels | new swizzle, instant | new frame |
| anything else (other channels, `range`) | re-run conversion from the source: GPU convert bands from the mmap, convert pass from the decoded data, or a new decode for `StreamUpload` | new frame |

## Load plan

The decision table at the top is a pure function, easy to unit-test:

```cpp
enum class Mode { Fast, Normal };

enum class Level0Source {
    SourceMmap,   // uncompressed: the source file already is level 0, no copy
    DecodeToDisk, // encoded: decode into a raw level-0 file in the cache
};

struct LoadPlan {
    Mode mode;
    FastStrategy fast;      // valid if mode == Fast
    Level0Source level0;    // valid if mode == Normal
    TextureFormat texture;  // display texture (fast) or frame texture (normal)
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
| `Level0FileSink` | Normal: DecodeToDisk | Writes rows into the raw level-0 file in the cache. Offers a writable mmap of that file as `full_image_buffer()`, so non-streamable decoders can decode images larger than RAM. |
| `PyramidBuilderSink` | Normal | Consumes rows and cascades 2× downsampling into levels 1…N while level 0 is still being produced. |
| `TeeSink` | Normal | Sends the same rows to `Level0FileSink` and `PyramidBuilderSink`. |

With `TeeSink`, decoding and pyramid building happen in a single pass: coarse levels become available sooner and level 0 is never read back from disk. Running them sequentially also works and is simpler to start with.

## Threads

```
 decode thread       ImageSource::decode ──► RowSink (TextureUpload / Level0 / Pyramid)
 pyramid thread      (if not tee'd) level0 mmap ──► pyramid levels on disk
 view sampler pool   FrameRequest ──► pyramid / level0 mmap ──► converted Frame bands ──► UploadQueue
                                                  │
 main thread (GL)    UploadQueue.pop() up to 64 MB per wake-up ──► glTexSubImage2D ──► image shader
                     DirectUpload: glTexSubImage2D from the mmap
                     GpuConvert: mmap band ──► RawBuffer ──► convert pass ──► FBO texture (band by band, once)
                     GpuDecode: encoded upload ──► decode pass ──► convert pass (once)
                     mipmaps: MipmapBuilder, ~4 M texels of level blits per wake-up
                     ▲ woken by RGFW_stopCheckEvents() whenever something is queued
```

All GL calls stay on the main thread; workers only hand over CPU buffers. `GpuConvert` needs no worker: the mmap is already readable, so the main thread uploads and converts one band per wake-up and redraws in between. A single `UploadQueue` type serves every mode. For `StreamUpload` the decode thread is a `std::jthread` owned by `FastRenderer` (a `StreamJob` holding the converter, the queue, the sink and the thread, destroyed thread first); it moves to the `Loader` session once normal mode needs several workers. In normal mode, `TiledRenderer::update(view)` posts a new `FrameRequest` whenever the view, the framebuffer size or the display parameters change; bands from an older generation are dropped.

## Coordinates

`ViewTransform` is the shared camera. Only `Window` mutates it (pan, zoom, fit, resize); renderers receive it as `const&` each frame, and the normal mode renderer copies it into each `FrameRequest`. All positions are in level-0 image pixels, top-down; conversion to pyramid levels happens inside the view sampler. Bottom-up sources are flipped through the texture coordinates (fast mode direct upload), by the convert shader (`GpuConvert`), or by the CPU converter / pyramid builder (everything else). `image_size` is set once from `renderer->image_size()` after loading.

## Directory layout

```
src/
  main.cpp
  core/        errors, logging, linalgebra, settings/ (+ fast_mode_max_bytes)
               thread_queue.hpp    MPSC queue
               thread_pool.*       view sampler bands
               wakeup.*            wraps RGFW_stopCheckEvents
               cache_dir.*         cache path keyed by hash(path, size, mtime)
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
    pixel_converter.*    native rows → display texture rows (StreamUpload, ViewSampler)
    texture_upload_sink.*
    level0_file_sink.*   writable mmap
    tee_sink.hpp
  pyramid/
    pyramid_format.hpp   on-disk header (with PixelLayout), level table, tile index
    pyramid_builder.*    native-type downsampling, also a RowSink
    pyramid_store.*      mmap, get_tile(TileKey), which levels/tiles are ready
    tile_selection.*     ViewTransform → level + TileKeys, pure
    view_sampler.*       FrameRequest → Frame (resample, convert, preview fallback)
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
    tiled_renderer.*     posts FrameRequests, uploads Frame bands, draws the current/stale frame
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
- **Normal mode latency:** every pan, zoom or display change needs a new CPU frame. Stale-frame drawing hides it during interaction, but newly exposed borders stay empty until the frame arrives.
- **GPU-decoded peak memory:** encoded, decoded and converted textures can coexist during the passes; `transient_bytes` accounts for it in the budget.
- **Rows not loaded yet with RGB textures:** the texture is cleared to transparent, but an RGB texture's swizzle forces alpha to 1, so missing rows show black instead of the background color (`StreamUpload` and `GpuConvert` while loading, a truncated file afterwards).
- **`GpuConvert` and 3 channels:** the convert pass renders into an FBO, so 3-channel results are stored as RGBA (RGB is not guaranteed renderable). Direct upload keeps true RGB textures.
