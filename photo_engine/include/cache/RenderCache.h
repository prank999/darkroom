#pragma once

#include "document/ImageSource.h"
#include "adjustments/AdjustmentState.h"
#include "rendering/RenderedImage.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace photo {

/**
 * Cache key for rendered tiles.
 */
struct CacheKey {
    size_t sourceHash;
    AdjustmentState adjustments;
    float scale;
    
    bool operator==(const CacheKey& other) const {
        return sourceHash == other.sourceHash &&
               adjustments == other.adjustments &&
               scale == other.scale;
    }
};

/**
 * Hash function for CacheKey.
 */
struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const {
        size_t h1 = std::hash<size_t>{}(key.sourceHash);
        size_t h2 = std::hash<float>{}(key.scale);
        
        // Simple hash of adjustment state
        size_t h3 = 0;
        h3 ^= std::hash<float>{}(key.adjustments.exposure) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.contrast) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.highlights) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.shadows) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.whites) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.blacks) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.temperature) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.tint) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.vibrance) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        h3 ^= std::hash<float>{}(key.adjustments.saturation) + 0x9e3779b9 + (h3 << 6) + (h3 >> 2);
        
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

/**
 * Render cache for storing previously rendered results.
 * Uses LRU eviction policy.
 */
class RenderCache {
public:
    explicit RenderCache(size_t maxBytes = 512 * 1024 * 1024);  // 512MB default
    
    /**
     * Try to get a cached render.
     * @return Shared pointer to cached image, or nullptr if not found.
     */
    std::shared_ptr<RenderedImage> get(const CacheKey& key);
    
    /**
     * Store a render in the cache.
     */
    void put(const CacheKey& key, std::shared_ptr<RenderedImage> image);
    
    /**
     * Clear all cached data.
     */
    void clear();
    
    /**
     * Get current cache size in bytes.
     */
    size_t sizeBytes() const;
    
    /**
     * Get number of cached items.
     */
    size_t itemCount() const;

private:
    struct CacheEntry {
        std::shared_ptr<RenderedImage> image;
        size_t sizeBytes;
        uint64_t lastAccess;
    };
    
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;
    size_t maxBytes_;
    size_t currentBytes_ = 0;
    uint64_t accessCounter_ = 0;
    mutable std::mutex mutex_;
    
    void evictIfNeeded();
};

} // namespace photo
