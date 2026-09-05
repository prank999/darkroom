#include "cache/RenderCache.h"

namespace photo {

RenderCache::RenderCache(size_t maxBytes)
    : maxBytes_(maxBytes) {}

std::shared_ptr<RenderedImage> RenderCache::get(const CacheKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Update access time for LRU
        it->second.lastAccess = ++accessCounter_;
        return it->second.image;
    }
    
    return nullptr;
}

void RenderCache::put(const CacheKey& key, std::shared_ptr<RenderedImage> image) {
    if (!image || !image->isValid()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Calculate size in bytes
    size_t imgBytes = image->data().size() * sizeof(float);
    
    // Check if already cached
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Update existing entry
        currentBytes_ -= it->second.sizeBytes;
        it->second.image = image;
        it->second.sizeBytes = imgBytes;
        it->second.lastAccess = ++accessCounter_;
        currentBytes_ += imgBytes;
        return;
    }
    
    // Add new entry
    CacheEntry entry{image, imgBytes, ++accessCounter_};
    cache_[key] = entry;
    currentBytes_ += imgBytes;
    
    // Evict if necessary
    evictIfNeeded();
}

void RenderCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    currentBytes_ = 0;
}

size_t RenderCache::sizeBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentBytes_;
}

size_t RenderCache::itemCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

void RenderCache::evictIfNeeded() {
    // Simple LRU: remove least recently accessed items
    while (currentBytes_ > maxBytes_ && !cache_.empty()) {
        CacheKey lruKey{};
        uint64_t minAccess = UINT64_MAX;
        
        for (const auto& [key, entry] : cache_) {
            if (entry.lastAccess < minAccess) {
                minAccess = entry.lastAccess;
                lruKey = key;
            }
        }
        
        if (minAccess != UINT64_MAX) {
            currentBytes_ -= cache_[lruKey].sizeBytes;
            cache_.erase(lruKey);
        } else {
            break;
        }
    }
}

} // namespace photo
