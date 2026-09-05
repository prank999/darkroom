#include "render/renderer.hpp"
#include "render/cache.hpp"
#include <chrono>

namespace pe {

CPURenderer::CPURenderer() 
    : cache_(std::make_unique<RenderCache>(512)),
      pipeline_(std::make_unique<RenderPipeline>()) {
    
    initWorkers(-1);  // Auto-detect
}

CPURenderer::~CPURenderer() {
    shutdown_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

void CPURenderer::initWorkers(int num_threads) {
    if (num_threads <= 0) {
        num_threads = static_cast<int>(std::thread::hardware_concurrency());
        if (num_threads == 0) num_threads = 4;
    }
    
    shutdown_.store(false);
    
    for (int i = 0; i < num_threads; ++i) {
        WorkerThread worker;
        worker.thread = std::thread(&CPURenderer::workerLoop, this);
        workers_.push_back(std::move(worker));
    }
}

void CPURenderer::workerLoop() {
    while (!shutdown_.load(std::memory_order_acquire)) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return shutdown_.load(std::memory_order_acquire) || !task_queue_.empty();
            });
            
            if (shutdown_.load(std::memory_order_acquire) && task_queue_.empty()) {
                return;
            }
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }
        
        if (task) {
            task();
        }
    }
}

void CPURenderer::updatePreviewProxy(const EncodedImage& source, uint32_t max_dim) {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    preview_proxy_ = PreviewProxy::create(source, max_dim);
}

RenderResult CPURenderer::render(const EncodedImage& source, const AdjustmentState& state,
                                  const RenderOptions& options) {
    CancelToken cancel;
    return render(source, state, options, cancel);
}

RenderResult CPURenderer::render(const EncodedImage& source, const AdjustmentState& state,
                                  const RenderOptions& options, CancelToken& cancel) {
    RenderResult result;
    
    // Determine which source to use (full-res or proxy)
    const EncodedImage* render_source = &source;
    
    if (options.preview_mode && preview_proxy_.isValid()) {
        render_source = &preview_proxy_.lowResSource();
    }
    
    uint32_t width = render_source->width();
    uint32_t height = render_source->height();
    
    result.width = width;
    result.height = height;
    
    // Generate cache key components
    size_t source_hash = std::hash<std::string>{}(render_source->filename());
    size_t adjust_hash = state.hash();
    uint64_t generation = ++generation_counter_;
    current_generation_.store(generation, std::memory_order_release);
    
    // Check if we can serve from cache (only for non-preview, full tiles)
    if (options.use_cache && !options.preview_mode) {
        CacheKey cache_key{source_hash, adjust_hash, 0, 0, 0};
        std::shared_ptr<LinearImageBuffer> cached;
        
        if (cache_->get(cache_key, cached)) {
            result.buffer = *cached;
            result.success = true;
            return result;
        }
    }
    
    // Create output buffer
    LinearImageBuffer output(width, height, false);
    
    // Convert source to linear buffer
    {
        FloatPixel* out_data = output.data();
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                out_data[y * width + x] = render_source->getPixelLinear(x, y);
            }
        }
    }
    
    // Process in tiles for large images
    uint32_t tile_size = options.tile_size;
    uint32_t num_tiles_x = (width + tile_size - 1) / tile_size;
    uint32_t num_tiles_y = (height + tile_size - 1) / tile_size;
    
    bool cancelled = false;
    
    for (uint32_t ty = 0; ty < num_tiles_y && !cancelled; ++ty) {
        for (uint32_t tx = 0; tx < num_tiles_x && !cancelled; ++tx) {
            if (cancel.isCancelled()) {
                cancelled = true;
                break;
            }
            
            uint32_t tile_x = tx * tile_size;
            uint32_t tile_y = ty * tile_size;
            uint32_t tile_w = std::min(tile_size, width - tile_x);
            uint32_t tile_h = std::min(tile_size, height - tile_y);
            
            // Check cache for this tile
            if (options.use_cache) {
                CacheKey tile_key{source_hash, adjust_hash, tile_x, tile_y, 0};
                std::shared_ptr<LinearImageBuffer> cached_tile;
                
                if (cache_->get(tile_key, cached_tile)) {
                    // Copy cached tile to output
                    for (uint32_t y = 0; y < tile_h; ++y) {
                        FloatPixel* src_row = cached_tile->row(y);
                        FloatPixel* dst_row = output.row(tile_y + y);
                        std::memcpy(dst_row + tile_x, src_row, tile_w * sizeof(FloatPixel));
                    }
                    continue;
                }
            }
            
            // Process tile through pipeline
            pipeline_->executeTile(output, tile_x, tile_y, tile_w, tile_h, state);
            
            // Cache the tile
            if (options.use_cache) {
                auto tile_buffer = std::make_shared<LinearImageBuffer>(tile_w, tile_h, false);
                for (uint32_t y = 0; y < tile_h; ++y) {
                    FloatPixel* src_row = output.row(tile_y + y);
                    FloatPixel* dst_row = tile_buffer->row(y);
                    std::memcpy(dst_row, src_row + tile_x, tile_w * sizeof(FloatPixel));
                }
                
                CacheKey tile_key{source_hash, adjust_hash, tile_x, tile_y, 0};
                cache_->put(tile_key, tile_buffer);
            }
        }
    }
    
    if (cancelled) {
        result.cancelled = true;
        result.success = false;
        return result;
    }
    
    // Clamp and validate output
    output.clamp(0.0f, 65504.0f);
    
    if (output.hasInvalidValues()) {
        result.error_message = "Invalid pixel values detected";
        result.success = false;
        return result;
    }
    
    result.buffer = std::move(output);
    result.success = true;
    
    return result;
}

void CPURenderer::invalidateCache() {
    cache_->clear();
}

} // namespace pe
