#include "document/Document.h"
#include "document/ImageSource.h"
#include <algorithm>

namespace photo {

Document::Document(std::shared_ptr<ImageSource> source)
    : source_(std::move(source)) {
    // Initialize undo stack with initial state
    undoStack_.push_back(adjustments_);
}

std::shared_ptr<Document> Document::loadFromFile(const std::string& path) {
    auto source = ImageSource::loadFromFile(path);
    return std::make_shared<Document>(source);
}

void Document::setAdjustments(const AdjustmentState& adj) {
    pushUndo();
    adjustments_ = adj;
    notifyChanged();
}

bool Document::undo() {
    if (!canUndo()) {
        return false;
    }
    
    --undoIndex_;
    adjustments_ = undoStack_[undoIndex_];
    notifyChanged();
    return true;
}

bool Document::redo() {
    if (!canRedo()) {
        return false;
    }
    
    ++undoIndex_;
    adjustments_ = undoStack_[undoIndex_];
    notifyChanged();
    return true;
}

void Document::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    undoStack_.clear();
    undoStack_.push_back(adjustments_);
    undoIndex_ = 0;
}

std::shared_ptr<RenderedImage> Document::renderFull() {
    RenderOptions options;
    options.scale = 1.0f;
    options.previewMode = false;
    options.highQuality = true;
    
    return renderer_.render(source_, adjustments_, options);
}

std::shared_ptr<RenderedImage> Document::renderPreview(int maxDimension) {
    float scale = calculatePreviewScale(maxDimension);
    
    RenderOptions options;
    options.scale = scale;
    options.previewMode = true;
    options.highQuality = false;  // Faster rendering for preview
    
    return renderer_.render(source_, adjustments_, options);
}

std::future<std::shared_ptr<RenderedImage>> Document::renderAsync(
    float scale, bool previewMode
) {
    RenderOptions options;
    options.scale = scale;
    options.previewMode = previewMode;
    options.highQuality = !previewMode;
    
    return renderer_.renderAsync(source_, adjustments_, options);
}

void Document::pushUndo() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Don't push duplicate states
    if (!undoStack_.empty() && undoStack_.back() == adjustments_) {
        return;
    }
    
    // Remove any redo states
    while (undoStack_.size() > undoIndex_ + 1) {
        undoStack_.pop_back();
    }
    
    // Push new state
    undoStack_.push_back(adjustments_);
    ++undoIndex_;
    
    // Limit stack size
    if (undoStack_.size() > MAX_UNDO_STATES) {
        undoStack_.erase(undoStack_.begin());
        --undoIndex_;
    }
}

void Document::notifyChanged() {
    // Clear cache when adjustments change
    cache_.clear();
}

float Document::calculatePreviewScale(int maxDimension) const {
    if (!source_) {
        return 1.0f;
    }
    
    int maxSrc = std::max(source_->width(), source_->height());
    if (maxSrc <= maxDimension) {
        return 1.0f;
    }
    
    return static_cast<float>(maxDimension) / maxSrc;
}

} // namespace photo
