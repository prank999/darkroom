#pragma once
/**
 * @file cache.hpp
 * @brief Tile-based render cache with LRU eviction
 * 
 * DESIGN:
 * - Cache stores intermediate rendered tiles, not just final images
 * - Key = (source_hash, adjustment_hash, tile_x, tile_y, zoom_level)
 * - LRU eviction when memory limit exceeded
 * - Thread-safe access
 */

#include "core/image.hpp"
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>

namespace pe {

struct CacheKey {
    size_t source_hash;
    size_t adjustment_hash;
    uint32_t tile_x, tile_y;
    uint32_t zoom_level;
    
    bool operator==(const CacheKey& o) const {
        return source_hash == o.source_hash && 
               adjustment_hash == o.adjustment_hash &&
               tile_x == o.tile_x && tile_y == o.tile_y &&
               zoom_level == o.zoom_level;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const {
        size_t h = k.source_hash;
        h ^= k.adjustment_hash + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= k.tile_x + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= k.tile_y + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= k.zoom_level + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct CachedTile {
    std::shared_ptr<LinearImageBuffer> buffer;
    size_t memory_bytes;
    uint64_t last_access_time;
};

class RenderCache {
private:
    std::unordered_map<CacheKey, CachedTile, CacheKeyHash> tiles_;
    std::list<CacheKey> lru_list_;
    std::mutex mutex_;
    
    size_t max_memory_bytes_ = 512 * 1024 * 1024;  // 512 MB default
    size_t current_memory_bytes_ = 0;
    uint64_t access_counter_ = 0;
    
    void evictIfNeeded();
    void touch(const CacheKey& key);
    
public:
    explicit RenderCache(size_t max_memory_mb = 512);
    
    bool get(const CacheKey& key, std::shared_ptr<LinearImageBuffer>& out);
    void put(const CacheKey& key, std::shared_ptr<LinearImageBuffer> buffer);
    void invalidate(const CacheKey& key);
    void clear();
    
    size_t getMemoryUsage() const { return current_memory_bytes_; }
    size_t getTileCount() const { return tiles_.size(); }
    void setMaxMemory(size_t bytes);
};

} // namespace pe
