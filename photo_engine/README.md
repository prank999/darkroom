# Photo Editing Engine

A professional, non-destructive photo editing engine implemented in C++20.

## Architecture Overview

```
photo_engine/
├── include/
│   ├── document/          # Document model (immutable source, adjustments)
│   │   ├── ImageSource.h  # Immutable source image
│   │   └── Document.h     # Main document with undo/redo
│   ├── adjustments/       # Adjustment parameters and curves
│   │   ├── AdjustmentState.h
│   │   └── ToneCurve.h
│   ├── rendering/         # Rendering pipeline
│   │   ├── Renderer.h
│   │   └── RenderedImage.h
│   ├── color/             # Color space conversions
│   │   └── ColorSpace.h
│   ├── cache/             # Render caching
│   │   └── RenderCache.h
│   └── utils/
├── src/                   # Implementation files
├── tests/                 # Automated test suite
├── benchmarks/            # Performance benchmarks
└── third_party/           # External dependencies (stb_image)
```

## Key Design Decisions

### 1. Non-Destructive Editing
- **ImageSource** is completely immutable after creation
- All adjustments are stored as parameters in **AdjustmentState**
- Rendering always starts from the original source, never from previous renders
- This ensures perfect undo/redo and branchable edit histories

### 2. Linear Light Processing
- Images are converted from sRGB to linear light on load
- All adjustments (especially exposure) operate in linear space
- This matches real photographic behavior where exposure doubles/halves light
- Final output is converted back to sRGB for display

### 3. Adjustment Pipeline Order
The rendering pipeline applies adjustments in this order:
1. Exposure (linear light multiplication)
2. White Balance (temperature/tint)
3. Highlights/Shadows (tone-aware)
4. Whites/Blacks (extreme tones)
5. Contrast (luminance-based sigmoid)
6. Saturation/Vibrance (HSL-based)
7. Tone Curves (per-channel or composite)
8. sRGB conversion for output

### 4. Tone Curve System
- Supports RGB composite curve and individual R, G, B curves
- Control points with linear interpolation
- Lookup table optimization for fast evaluation
- Identity detection for skipped processing

### 5. Undo/Redo
- Stores only **AdjustmentState** structs (~100 bytes each)
- No image data copied for undo history
- Default 50-state history limit
- O(1) undo/redo operations

### 6. Preview vs Final Rendering
- Same adjustment semantics for both
- Preview uses downsampling (scale < 1.0)
- Separate quality flags for future optimization
- Enables responsive UI with accurate preview

### 7. Async Rendering
- `renderAsync()` returns `std::future<RenderedImage>`
- Runs on background thread via `std::async`
- Supports cancellation
- UI thread never blocks

### 8. GPU Acceleration Path
The architecture supports GPU acceleration without model changes:
- **Renderer** interface can be reimplemented for GPU
- **AdjustmentState** is serializable for shader uniforms
- **ImageSource** can provide GPU textures
- Current CPU implementation serves as reference

## Color Management Assumptions

### Input/Output
- Input images assumed to be sRGB
- PNG/JPEG loaded via stb_image
- Internal processing in linear Rec.709 RGB

### Working Color Space
- Linear light (gamma 1.0)
- Rec.709 primaries
- D65 white point

### Limitations
- No ICC profile support yet
- No wide gamut (AdobeRGB, ProPhoto) support
- No tone mapping for HDR

These can be added by extending **ColorSpace** and **ImageSource**.

## Building

### Prerequisites
- CMake 3.16+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- pthread (Linux/macOS) or equivalent

### Build Commands

```bash
cd photo_engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Tests

```bash
./test_engine
```

### Run Benchmarks

```bash
./benchmark
```

## API Usage Example

```cpp
#include "document/Document.h"
#include "adjustments/AdjustmentState.h"

using namespace photo;

// Load image
auto doc = Document::loadFromFile("image.jpg");

// Apply adjustments
doc->adjust([](AdjustmentState& s) {
    s.exposure = 0.5f;      // +0.5 EV
    s.contrast = 20.0f;     // +20 contrast
    s.highlights = -30.0f;  // Recover highlights
    s.shadows = 25.0f;      // Lift shadows
    s.temperature = 10.0f;  // Warm up
    s.vibrance = 15.0f;     // Smart saturation
});

// Get preview (fast, downscaled)
auto preview = doc->renderPreview(2048);

// Get full resolution render
auto full = doc->renderFull();

// Undo/redo
doc->undo();   // Revert last adjustment
doc->redo();   // Restore undone adjustment

// Async rendering
auto future = doc->renderAsync(1.0f, false);
// ... do other work ...
auto result = future.get();  // Blocks until complete
```

## Extensibility

### RAW Processing
Add a `RawSource` class that:
- Uses libraw or similar for demosaicing
- Applies camera white balance metadata
- Outputs linear RGB like current ImageSource

### Masking/Local Adjustments
Extend AdjustmentState with:
```cpp
struct LocalAdjustment {
    std::shared_ptr<Mask> mask;
    AdjustmentState adjustments;
};
std::vector<LocalAdjustment> localAdjustments;
```

### AI Features
- Integrate ONNX Runtime or similar
- Add AI operations as adjustment types
- Process on GPU while keeping CPU fallback

### GPU Acceleration
1. Create `GpuRenderer` implementing same interface
2. Use Vulkan/DirectX/Metal compute shaders
3. Upload ImageSource as texture
4. Pass AdjustmentState as uniform buffer
5. Implement same pipeline in GLSL/HLSL

### 100+ MP Images
For extremely large images:
1. **Tiled rendering**: Process image in tiles
2. **Out-of-core**: Store tiles on disk, load as needed
3. **Pyramid/cache**: Pre-build mipmaps for zoom levels
4. **Parallel processing**: Split tiles across threads/GPU

Current limitations:
- Entire image must fit in RAM
- Single-threaded pixel processing
- These are intentional for simplicity and can be addressed incrementally

## Limitations

1. **No GPU acceleration** - Pure CPU implementation (by design for clarity)
2. **Single-threaded rendering** - Can be parallelized with OpenMP/TBB
3. **No color profiles** - Assumes sRGB input/output
4. **No HDR support** - LDR only (0-1 range)
5. **No layer support** - Single image only
6. **Basic interpolation** - Bilinear only, no Lanczos/etc.

These are architectural choices to keep the prototype focused. Each can be added without changing the core model.

## Test Coverage

Tests verify:
- Source image immutability
- Identity adjustment baseline
- Exposure luminance response
- Contrast tonal distribution
- Highlights/shadows behavior
- Temperature/tint color shifts
- Vibrance vs saturation difference
- Tone curve application
- RGB channel isolation
- Undo/redo correctness
- Deterministic rendering
- Preview/final consistency
- No NaN/Inf production
- Output dimensions
- Cache functionality
- Async rendering

## Performance Notes

Benchmark on 24MP (6000x4000) image:
- Preview (25%): ~50-100ms depending on adjustments
- Full resolution: ~500-2000ms depending on adjustments
- Memory: ~288MB for source + ~96MB for full render

Optimization opportunities:
- SIMD vectorization (SSE/AVX)
- Multi-threaded pixel processing
- GPU compute shaders
- Lookup table precomputation
- Region-of-interest rendering
