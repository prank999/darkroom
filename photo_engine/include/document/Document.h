#pragma once

#include "adjustments/AdjustmentState.h"
#include "rendering/RenderedImage.h"
#include "rendering/Renderer.h"
#include "cache/RenderCache.h"
#include "document/ImageSource.h"
#include <memory>
#include <vector>
#include <mutex>

namespace photo {

/**
 * Document represents an editable photo with its adjustment history.
 * Manages undo/redo through adjustment states, NOT image copies.
 */
class Document {
public:
    Document() = default;
    
    /**
     * Create document from source image.
     */
    explicit Document(std::shared_ptr<ImageSource> source);
    
    // Load from file
    static std::shared_ptr<Document> loadFromFile(const std::string& path);
    
    /**
     * Get the immutable source image.
     */
    std::shared_ptr<ImageSource> source() const { return source_; }
    
    /**
     * Get current adjustment state.
     */
    const AdjustmentState& adjustments() const { return adjustments_; }
    
    /**
     * Set new adjustment state (creates undo point).
     */
    void setAdjustments(const AdjustmentState& adj);
    
    /**
     * Apply a single adjustment change.
     */
    template<typename Func>
    void adjust(Func&& f) {
        pushUndo();
        f(adjustments_);
        notifyChanged();
    }
    
    /**
     * Undo last change.
     * @return true if undo was performed
     */
    bool undo();
    
    /**
     * Redo last undone change.
     * @return true if redo was performed
     */
    bool redo();
    
    /**
     * Check if undo is available.
     */
    bool canUndo() const { return undoIndex_ > 0; }
    
    /**
     * Check if redo is available.
     */
    bool canRedo() const { return undoIndex_ < undoStack_.size() - 1; }
    
    /**
     * Clear undo/redo history.
     */
    void clearHistory();
    
    /**
     * Render at full resolution.
     */
    std::shared_ptr<RenderedImage> renderFull();
    
    /**
     * Render preview at reduced resolution.
     * @param maxDimension Maximum dimension for preview
     */
    std::shared_ptr<RenderedImage> renderPreview(int maxDimension = 2048);
    
    /**
     * Render asynchronously.
     */
    std::future<std::shared_ptr<RenderedImage>> renderAsync(
        float scale = 1.0f,
        bool previewMode = false
    );
    
    /**
     * Get renderer for customization.
     */
    Renderer& renderer() { return renderer_; }
    
    /**
     * Get cache for configuration.
     */
    RenderCache& cache() { return cache_; }
    
    /**
     * Check if document has changes from identity.
     */
    bool hasAdjustments() const { return !adjustments_.isIdentity(); }

private:
    std::shared_ptr<ImageSource> source_;
    AdjustmentState adjustments_;
    Renderer renderer_;
    RenderCache cache_;
    
    // Undo/redo stack (stores adjustment states only)
    std::vector<AdjustmentState> undoStack_;
    size_t undoIndex_ = 0;
    static constexpr size_t MAX_UNDO_STATES = 50;
    
    mutable std::mutex mutex_;
    
    void pushUndo();
    void notifyChanged();
    
    float calculatePreviewScale(int maxDimension) const;
};

} // namespace photo
