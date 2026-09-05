#include "render/cache.hpp"

namespace pe {

RenderCache::RenderCache(size_t max_memory_mb) 
    : max_memory_bytes_(max_memory_mb * 1024 * 1024) {
}

void RenderCache::touch(const CacheKey& key) {
    ++access_counter_;
    
    auto it = tiles_.find(key);
    if (it != tiles_.end()) {
        it->second.last_access_time = access_counter_;
        
        // Move to front of LRU list
        lru_list_.remove(key);
        lru_list_.push_front(key);
    }
}

void RenderCache::evictIfNeeded() {
    while (current_memory_bytes_ > max_memory_bytes_ && !lru_list_.empty()) {
        CacheKey key_to_evict = lru_list_.back();
        lru_list_.pop_back();
        
        auto it = tiles_.find(key_to_evict);
        if (it != tiles_.end()) {
            current_memory_bytes_ -= it->second.memory_bytes;
            tiles_.erase(it);
        }
    }
}

bool RenderCache::get(const CacheKey& key, std::shared_ptr<LinearImageBuffer>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tiles_.find(key);
    if (it == tiles_.end()) return false;
    
    touch(key);
    out = it->second.buffer;
    return true;
}

void RenderCache::put(const CacheKey& key, std::shared_ptr<LinearImageBuffer> buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t new_tile_size = buffer->pixelCount() * sizeof(FloatPixel);
    
    // Remove existing entry if present
    auto it = tiles_.find(key);
    if (it != tiles_.end()) {
        current_memory_bytes_ -= it->second.memory_bytes;
        lru_list_.remove(key);
    }
    
    CachedTile tile;
    tile.buffer = buffer;
    tile.memory_bytes = new_tile_size;
    tile.last_access_time = ++access_counter_;
    
    tiles_[key] = tile;
    current_memory_bytes_ += new_tile_size;
    lru_list_.push_front(key);
    
    evictIfNeeded();
}

void RenderCache::invalidate(const CacheKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tiles_.find(key);
    if (it != tiles_.end()) {
        current_memory_bytes_ -= it->second.memory_bytes;
        lru_list_.remove(key);
        tiles_.erase(it);
    }
}

void RenderCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    tiles_.clear();
    lru_list_.clear();
    current_memory_bytes_ = 0;
}

void RenderCache::setMaxMemory(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_memory_bytes_ = bytes;
    evictIfNeeded();
}

} // namespace pe
