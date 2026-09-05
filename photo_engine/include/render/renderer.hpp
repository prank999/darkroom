#pragma once
/**
 * @file renderer.hpp
 * @brief Professional render engine with threading, caching, and cancellation
 * 
 * ARCHITECTURAL DECISIONS:
 * 
 * 1. THREAD POOL:
 *    - Fixed number of worker threads (not uncontrolled std::async)
 *    - Work stealing for load balancing
 *    - Thread-safe task queue
 * 
 * 2. RENDER CACHING:
 *    - Tile-based cache (not full-image only)
 *    - LRU eviction policy
 *    - Cache key includes adjustment state hash
 *    - Invalidates on parameter change
 * 
 * 3. CANCELLATION:
 *    - Atomic flag checked during processing
 *    - Stale renders cannot overwrite newer results
 *    - Generation counter prevents race conditions
 */

#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include "pipeline/stages.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <functional>

namespace pe {

// Forward declarations
class RenderCache;

struct RenderOptions {
    bool preview_mode = false;
    uint32_t preview_max_dim = 2048;
    bool use_cache = true;
    uint32_t tile_size = 512;  // For tiled rendering
    int num_threads = -1;      // -1 = auto-detect
};

struct RenderResult {
    LinearImageBuffer buffer;
    uint32_t width = 0;
    uint32_t height = 0;
    bool success = false;
    bool cancelled = false;
    std::string error_message;
    
    FloatPixel getPixel(uint32_t x, uint32_t y) const {
        if (!success || x >= width || y >= height) return FloatPixel();
        return buffer.at(x, y);
    }
};

class CancelToken {
private:
    std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
public:
    void cancel() { cancelled_->store(true, std::memory_order_relaxed); }
    bool isCancelled() const { return cancelled_->load(std::memory_order_relaxed); }
    void reset() { cancelled_->store(false, std::memory_order_relaxed); }
};

class CPURenderer {
private:
    struct WorkerThread {
        std::thread thread;
        std::atomic<bool> busy{false};
    };
    
    std::vector<WorkerThread> workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> shutdown_{false};
    
    std::unique_ptr<RenderCache> cache_;
    std::unique_ptr<RenderPipeline> pipeline_;
    
    std::atomic<uint64_t> generation_counter_{0};
    std::atomic<uint64_t> current_generation_{0};
    
    PreviewProxy preview_proxy_;
    std::mutex proxy_mutex_;
    
    void initWorkers(int num_threads);
    void workerLoop();
    
public:
    CPURenderer();
    ~CPURenderer();
    
    // Main render entry point
    RenderResult render(const EncodedImage& source, const AdjustmentState& state,
                       const RenderOptions& options = RenderOptions());
    
    // Render with cancellation support
    RenderResult render(const EncodedImage& source, const AdjustmentState& state,
                       const RenderOptions& options, CancelToken& cancel);
    
    // Get cache reference
    RenderCache& getCache() { return *cache_; }
    const RenderCache& getCache() const { return *cache_; }
    
    // Invalidate all cached data
    void invalidateCache();
    
    // Set preview proxy (for fast preview mode)
    void updatePreviewProxy(const EncodedImage& source, uint32_t max_dim = 2048);
};

} // namespace pe
