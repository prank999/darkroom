#pragma once
#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include <memory>
#include <atomic>
#include <functional>
#include <future>
#include <map>

namespace pe {

/**
 * RenderResult - Container for rendered image data
 */
struct RenderResult {
    std::vector<float> data;  // Interleaved RGB float values in linear space
    uint32_t width = 0;
    uint32_t height = 0;
    bool success = false;
    std::string error_message;
    
    // Convert to 8-bit sRGB for display/output
    std::vector<uint8_t> toSRGBU8() const;
    
    // Get pixel at (x, y)
    Pixel32 getPixel(uint32_t x, uint32_t y) const {
        if (x >= width || y >= height) return Pixel32();
        size_t idx = (y * width + x) * 3;
        return Pixel32(data[idx], data[idx + 1], data[idx + 2]);
    }
};

/**
 * RenderOptions - Control rendering behavior
 */
struct RenderOptions {
    bool preview_mode = false;           // Use reduced resolution
    uint32_t preview_max_dim = 1920;     // Max dimension for preview
    bool use_cache = true;               // Enable render caching
    std::function<void(float)> progress_callback;  // Progress reporting (0-1)
    std::atomic<bool>* cancel_flag = nullptr;  // For async cancellation
};

/**
 * RenderCache - Cache rendered tiles/results for faster re-rendering
 * 
 * Uses a simple LRU strategy with configurable size limit.
 */
class RenderCache {
public:
    struct CacheKey {
        size_t state_hash;
        uint32_t width;
        uint32_t height;
        bool is_preview;
        
        bool operator==(const CacheKey& other) const {
            return state_hash == other.state_hash &&
                   width == other.width &&
                   height == other.height &&
                   is_preview == other.is_preview;
        }
    };
    
    struct CacheEntry {
        RenderResult result;
        uint64_t last_used;
    };
    
private:
    static constexpr size_t MAX_CACHE_ENTRIES = 16;
    static constexpr size_t MAX_CACHE_MEMORY_MB = 512;
    
    std::map<size_t, CacheEntry> cache_;  // Simplified: hash -> entry
    size_t current_memory_usage_ = 0;
    uint64_t access_counter_ = 0;
    
    size_t computeStateHash(const AdjustmentState& state) const;

public:
    std::optional<RenderResult> get(const CacheKey& key);
    void put(const CacheKey& key, const RenderResult& result);
    void clear();
    size_t getMemoryUsage() const { return current_memory_usage_; }
    
    // Public accessor for hash computation
    size_t publicComputeStateHash(const AdjustmentState& state) const {
        return computeStateHash(state);
    }
};

/**
 * IRenderer - Abstract renderer interface
 * 
 * This abstraction allows swapping between CPU and GPU implementations
 * without changing the document/adjustment model.
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;
    
    // Synchronous render
    virtual RenderResult render(const SourceImage& source,
                                const AdjustmentState& state,
                                const RenderOptions& options) = 0;
    
    // Asynchronous render (returns future)
    virtual std::future<RenderResult> renderAsync(const SourceImage& source,
                                                   const AdjustmentState& state,
                                                   const RenderOptions& options);
    
    // Check if renderer supports GPU acceleration
    virtual bool isGPUAccelerated() const { return false; }
};

/**
 * CPU Renderer Implementation
 * 
 * High-quality CPU-based rendering pipeline with:
 * - Linear light processing
 * - Proper tone mapping
 * - Parallel tile-based processing
 * - SIMD-friendly data layout
 */
class CPURenderer : public IRenderer {
private:
    RenderCache cache_;
    
    // Internal processing functions (implemented in .cpp)
    void processTile(const SourceImage& source,
                     const AdjustmentState& state,
                     float* output_buffer,
                     uint32_t tile_x, uint32_t tile_y,
                     uint32_t tile_w, uint32_t tile_h,
                     const RenderOptions& options);
    
    Pixel32 applyAdjustments(Pixel32 pixel, const AdjustmentState& state) const;
    
public:
    CPURenderer() = default;
    
    RenderResult render(const SourceImage& source,
                        const AdjustmentState& state,
                        const RenderOptions& options) override;
    
    bool isGPUAccelerated() const override { return false; }
    
    // Access cache for statistics
    const RenderCache& getCache() const { return cache_; }
    RenderCache& getCache() { return cache_; }
};

/**
 * Document - Manages source image and adjustment history
 * 
 * Implements undo/redo by storing AdjustmentState snapshots,
 * NOT complete image copies.
 */
class Document {
private:
    std::shared_ptr<SourceImage> source_image_;
    std::vector<AdjustmentState> history_;
    size_t current_index_ = 0;
    std::unique_ptr<IRenderer> renderer_;
    
    // Cached last render result for quick previews
    mutable RenderResult cached_render_;
    mutable size_t cached_state_index_ = SIZE_MAX;
    
public:
    Document();
    explicit Document(std::shared_ptr<SourceImage> source);
    
    // Load image from file
    static std::optional<Document> loadFromFile(const std::string& path);
    
    // Access source image (immutable)
    const SourceImage& getSourceImage() const { return *source_image_; }
    std::shared_ptr<SourceImage> getSourceImagePtr() const { return source_image_; }
    
    // Current adjustment state
    const AdjustmentState& getCurrentState() const;
    
    // Apply new adjustment state (creates new history entry)
    void applyState(const AdjustmentState& state);
    
    // Modify current state in place (for interactive adjustments)
    void modifyCurrentState(std::function<void(AdjustmentState&)> modifier);
    
    // Undo/Redo
    bool canUndo() const { return current_index_ > 0; }
    bool canRedo() const { return current_index_ < history_.size() - 1; }
    bool undo();
    bool redo();
    
    // Render current state
    RenderResult render(const RenderOptions& options = {}) const;
    
    // Async render
    std::future<RenderResult> renderAsync(const RenderOptions& options = {});
    
    // Set custom renderer
    void setRenderer(std::unique_ptr<IRenderer> renderer) {
        renderer_ = std::move(renderer);
    }
    
    // Access renderer
    IRenderer* getRenderer() { return renderer_.get(); }
    const IRenderer* getRenderer() const { return renderer_.get(); }
    
    // History info
    size_t getHistorySize() const { return history_.size(); }
    size_t getCurrentIndex() const { return current_index_; }
};

} // namespace pe
