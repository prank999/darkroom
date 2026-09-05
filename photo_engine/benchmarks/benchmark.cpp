// Benchmark for photo editing engine
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstring>

#include "document/ImageSource.h"
#include "document/Document.h"
#include "adjustments/AdjustmentState.h"
#include "rendering/Renderer.h"
#include "color/ColorSpace.h"

using namespace photo;
using namespace std::chrono;

// Create a large test image (simulating 24MP = ~6000x4000)
std::shared_ptr<ImageSource> createLargeImage(int width, int height) {
    std::cout << "Creating " << width << "x" << height << " test image... ";
    std::cout.flush();
    
    std::vector<uint8_t> data(width * height * 4);
    
    // Create realistic gradient pattern with some variation
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            
            // Create varied pattern to prevent compression artifacts in processing
            float nx = static_cast<float>(x) / width;
            float ny = static_cast<float>(y) / height;
            
            data[idx + 0] = static_cast<uint8_t>(255 * (0.5f + 0.5f * nx));
            data[idx + 1] = static_cast<uint8_t>(255 * (0.5f + 0.5f * ny));
            data[idx + 2] = static_cast<uint8_t>(128 + 64 * std::sin(nx * 10) * std::cos(ny * 10));
            data[idx + 3] = 255;
        }
    }
    
    auto source = ImageSource::createFromData(data.data(), width, height, 4);
    std::cout << "Done (" << (width * height / 1000000.0f) << " MP)" << std::endl;
    
    return source;
}

void printSeparator() {
    std::cout << std::string(70, '=') << std::endl;
}

void printHeader(const std::string& title) {
    printSeparator();
    std::cout << title << std::endl;
    printSeparator();
}

int main() {
    printHeader("Photo Engine Benchmark Suite");
    
    // Create test images
    // 24MP is approximately 6000x4000
    const int WIDTH = 6000;
    const int HEIGHT = 4000;
    const double MEGAPIXELS = (WIDTH * HEIGHT) / 1000000.0;
    
    auto source = createLargeImage(WIDTH, HEIGHT);
    
    // Test adjustment states
    AdjustmentState identity;
    
    AdjustmentState moderate;
    moderate.exposure = 0.5f;
    moderate.contrast = 20.0f;
    moderate.highlights = -15.0f;
    moderate.shadows = 25.0f;
    moderate.temperature = 10.0f;
    moderate.saturation = 15.0f;
    
    AdjustmentState heavy;
    heavy.exposure = 1.0f;
    heavy.contrast = 40.0f;
    heavy.highlights = -50.0f;
    heavy.shadows = 50.0f;
    heavy.whites = 20.0f;
    heavy.blacks = -15.0f;
    heavy.temperature = 25.0f;
    heavy.tint = 10.0f;
    heavy.vibrance = 30.0f;
    heavy.saturation = 20.0f;
    heavy.rgbCurve = {
        {0.0f, 0.0f},
        {0.25f, 0.22f},
        {0.5f, 0.55f},
        {0.75f, 0.78f},
        {1.0f, 1.0f}
    };
    
    Renderer renderer;
    
    struct BenchmarkResult {
        std::string name;
        double timeMs;
        double mpPerSec;
    };
    
    std::vector<BenchmarkResult> results;
    
    // ========================================================================
    // Preview Render Benchmarks
    // ========================================================================
    printHeader("Preview Rendering (25% scale)");
    
    {
        RenderOptions opts;
        opts.scale = 0.25f;
        opts.previewMode = true;
        
        // Warm up
        auto warmup = renderer.render(source, identity, opts);
        
        // Benchmark
        auto start = high_resolution_clock::now();
        int iterations = 5;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, identity, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = (MEGAPIXELS * 0.25 * 0.25) / (elapsed / 1000.0);
        
        std::cout << "Identity adjustments:     " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(1) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Preview (identity)", elapsed, mpPerSec});
    }
    
    {
        RenderOptions opts;
        opts.scale = 0.25f;
        opts.previewMode = true;
        
        auto start = high_resolution_clock::now();
        int iterations = 5;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, moderate, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = (MEGAPIXELS * 0.25 * 0.25) / (elapsed / 1000.0);
        
        std::cout << "Moderate adjustments:     " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(1) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Preview (moderate)", elapsed, mpPerSec});
    }
    
    {
        RenderOptions opts;
        opts.scale = 0.25f;
        opts.previewMode = true;
        
        auto start = high_resolution_clock::now();
        int iterations = 5;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, heavy, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = (MEGAPIXELS * 0.25 * 0.25) / (elapsed / 1000.0);
        
        std::cout << "Heavy adjustments:        " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(1) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Preview (heavy)", elapsed, mpPerSec});
    }
    
    // ========================================================================
    // Full Resolution Render Benchmarks
    // ========================================================================
    printHeader("Full Resolution Rendering (100% scale)");
    
    {
        RenderOptions opts;
        opts.scale = 1.0f;
        opts.previewMode = false;
        opts.highQuality = true;
        
        // Warm up
        auto warmup = renderer.render(source, identity, opts);
        
        // Benchmark
        auto start = high_resolution_clock::now();
        int iterations = 3;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, identity, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = MEGAPIXELS / (elapsed / 1000.0);
        
        std::cout << "Identity adjustments:     " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(2) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Full (identity)", elapsed, mpPerSec});
    }
    
    {
        RenderOptions opts;
        opts.scale = 1.0f;
        opts.previewMode = false;
        opts.highQuality = true;
        
        auto start = high_resolution_clock::now();
        int iterations = 3;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, moderate, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = MEGAPIXELS / (elapsed / 1000.0);
        
        std::cout << "Moderate adjustments:     " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(2) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Full (moderate)", elapsed, mpPerSec});
    }
    
    {
        RenderOptions opts;
        opts.scale = 1.0f;
        opts.previewMode = false;
        opts.highQuality = true;
        
        auto start = high_resolution_clock::now();
        int iterations = 3;
        
        for (int i = 0; i < iterations; ++i) {
            auto result = renderer.render(source, heavy, opts);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count() / iterations;
        double mpPerSec = MEGAPIXELS / (elapsed / 1000.0);
        
        std::cout << "Heavy adjustments:        " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (" << std::setprecision(2) << mpPerSec << " MP/s)" << std::endl;
        results.push_back({"Full (heavy)", elapsed, mpPerSec});
    }
    
    // ========================================================================
    // Memory Usage Estimate
    // ========================================================================
    printHeader("Memory Usage Estimates");
    
    size_t sourceSize = source->data().size() * sizeof(float);
    size_t fullRenderSize = WIDTH * HEIGHT * 4 * sizeof(float);  // RGBA output
    size_t previewSize = (WIDTH/4) * (HEIGHT/4) * 4 * sizeof(float);
    
    std::cout << "Source image (linear RGB):  " << (sourceSize / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Full render (RGBA float):   " << (fullRenderSize / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Preview render (RGBA float): " << (previewSize / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Total (source + full + preview): " 
              << ((sourceSize + fullRenderSize + previewSize) / 1024 / 1024) << " MB" << std::endl;
    
    // ========================================================================
    // Async Rendering Test
    // ========================================================================
    printHeader("Async Rendering Test");
    
    {
        auto start = high_resolution_clock::now();
        
        auto future = renderer.renderAsync(source, moderate, RenderOptions{1.0f});
        
        // Do other work while rendering...
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        auto result = future.get();
        
        auto end = high_resolution_clock::now();
        double elapsed = duration<double, std::milli>(end - start).count();
        
        std::cout << "Async full render:        " << std::fixed << std::setprecision(1) 
                  << elapsed << " ms (non-blocking)" << std::endl;
        std::cout << "Result valid:             " << (result ? "Yes" : "No") << std::endl;
    }
    
    // ========================================================================
    // Summary
    // ========================================================================
    printHeader("Benchmark Summary");
    
    std::cout << std::left << std::setw(25) << "Test" 
              << std::right << std::setw(12) << "Time (ms)" 
              << std::setw(15) << "Throughput" << std::endl;
    printSeparator();
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.name
                  << std::right << std::setw(12) << std::fixed << std::setprecision(1) << r.timeMs
                  << std::setw(12) << std::setprecision(2) << r.mpPerSec << " MP/s" << std::endl;
    }
    
    printSeparator();
    std::cout << "Image size: " << WIDTH << "x" << HEIGHT << " (" 
              << std::fixed << std::setprecision(1) << MEGAPIXELS << " MP)" << std::endl;
    std::cout << "CPU: Single-threaded (can be parallelized)" << std::endl;
    
    return 0;
}
