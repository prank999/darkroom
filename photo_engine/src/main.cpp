#include <iostream>
#include <string>

#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include "render/renderer.hpp"

using namespace pe;

int main(int argc, char* argv[]) {
    std::cout << "Photo Engine CLI - Non-destructive Photo Editing Prototype\n";
    std::cout << "==========================================================\n\n";
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image_path> [output_path]\n";
        std::cout << "\nThis is a command-line interface for testing the photo engine.\n";
        std::cout << "It loads an image, applies some adjustments, and optionally saves the result.\n";
        return 1;
    }
    
    std::string input_path = argv[1];
    std::string output_path = (argc > 2) ? argv[2] : "";
    
    // Load image
    std::cout << "Loading image: " << input_path << "\n";
    auto source_opt = SourceImage::loadFromFile(input_path);
    
    if (!source_opt) {
        std::cerr << "Failed to load image: " << input_path << "\n";
        return 1;
    }
    
    SourceImage& source = *source_opt;
    std::cout << "Image loaded: " << source.width() << "x" << source.height() << " pixels\n";
    std::cout << "Channels: " << (int)source.channels() << "\n\n";
    
    // Create document
    Document doc(std::make_shared<SourceImage>(source));
    
    // Apply some test adjustments
    std::cout << "Applying adjustments:\n";
    
    AdjustmentState state;
    state.exposure = 0.5f;
    state.contrast = 15.0f;
    state.highlights = -20.0f;
    state.shadows = 30.0f;
    state.temperature = 5800.0f;
    state.vibrance = 20.0f;
    
    std::cout << "  Exposure: " << state.exposure << " EV\n";
    std::cout << "  Contrast: " << state.contrast << "\n";
    std::cout << "  Highlights: " << state.highlights << "\n";
    std::cout << "  Shadows: " << state.shadows << "\n";
    std::cout << "  Temperature: " << state.temperature << " K\n";
    std::cout << "  Vibrance: " << state.vibrance << "\n\n";
    
    doc.applyState(state);
    
    // Render preview
    std::cout << "Rendering preview...\n";
    RenderOptions preview_opts;
    preview_opts.preview_mode = true;
    preview_opts.preview_max_dim = 1920;
    
    RenderResult preview = doc.render(preview_opts);
    
    if (preview.success) {
        std::cout << "Preview rendered: " << preview.width << "x" << preview.height << " pixels\n";
    } else {
        std::cerr << "Preview render failed: " << preview.error_message << "\n";
    }
    
    // Render full resolution
    std::cout << "Rendering full resolution...\n";
    RenderOptions full_opts;
    full_opts.preview_mode = false;
    
    RenderResult full = doc.render(full_opts);
    
    if (full.success) {
        std::cout << "Full resolution rendered: " << full.width << "x" << full.height << " pixels\n";
        
        // Convert to sRGB U8 for output
        std::vector<uint8_t> srgb_data = full.toSRGBU8();
        std::cout << "Converted to sRGB: " << srgb_data.size() << " bytes\n";
    } else {
        std::cerr << "Full render failed: " << full.error_message << "\n";
    }
    
    // Test undo/redo
    std::cout << "\nTesting undo/redo:\n";
    std::cout << "  History size: " << doc.getHistorySize() << "\n";
    std::cout << "  Current index: " << doc.getCurrentIndex() << "\n";
    
    // Add another state
    AdjustmentState state2 = state;
    state2.exposure = 1.0f;
    doc.applyState(state2);
    
    std::cout << "  After applying new state:\n";
    std::cout << "    History size: " << doc.getHistorySize() << "\n";
    std::cout << "    Current index: " << doc.getCurrentIndex() << "\n";
    std::cout << "    Current exposure: " << doc.getCurrentState().exposure << "\n";
    
    doc.undo();
    std::cout << "  After undo:\n";
    std::cout << "    Current index: " << doc.getCurrentIndex() << "\n";
    std::cout << "    Current exposure: " << doc.getCurrentState().exposure << "\n";
    
    doc.redo();
    std::cout << "  After redo:\n";
    std::cout << "    Current index: " << doc.getCurrentIndex() << "\n";
    std::cout << "    Current exposure: " << doc.getCurrentState().exposure << "\n";
    
    std::cout << "\nDone!\n";
    
    return 0;
}
