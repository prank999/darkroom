#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>
#include <fstream>

#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include "render/renderer.hpp"

using namespace pe;
using namespace std::chrono;

// ============================================================================
// Generate a test image (synthetic gradient pattern)
// ============================================================================

SourceImage generateTestImage(uint32_t width, uint32_t height) {
    std::vector<uint8_t> data(width * height * 4);
    
    std::random_device rd;
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<> dist(0, 255);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            // Create a gradient with some noise
            float grad = static_cast<float>(x + y) / (width + height);
            
            uint8_t r = static_cast<uint8_t>(grad * 255.0f + (dist(gen) - 128) * 0.1f);
            uint8_t g = static_cast<uint8_t>((1.0f - grad) * 255.0f + (dist(gen) - 128) * 0.1f);
            uint8_t b = static_cast<uint8_t>(128 + (dist(gen) - 128) * 0.3f);
            
            size_t idx = (y * width + x) * 4;
            data[idx] = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
            data[idx + 3] = 255;
        }
    }
    
    return SourceImage::fromU8Data(data, width, height, 4);
}

// ============================================================================
// Benchmark function
// ============================================================================

struct BenchmarkResult {
    double preview_time_ms = 0;
    double full_time_ms = 0;
    size_t memory_bytes = 0;
    double pixels_per_second = 0;
};

BenchmarkResult runBenchmark(const SourceImage& source, const AdjustmentState& state, 
                             int iterations = 3) {
    BenchmarkResult result;
    
    CPURenderer renderer;
    
    // Warm up
    RenderOptions warm_opts;
    warm_opts.use_cache = false;
    renderer.render(source, state, warm_opts);
    
    // Preview benchmark
    RenderOptions preview_opts;
    preview_opts.preview_mode = true;
    preview_opts.preview_max_dim = 1920;
    preview_opts.use_cache = false;
    
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        renderer.render(source, state, preview_opts);
    }
    auto end = high_resolution_clock::now();
    result.preview_time_ms = duration<double, std::milli>(end - start).count() / iterations;
    
    // Full resolution benchmark
    RenderOptions full_opts;
    full_opts.preview_mode = false;
    full_opts.use_cache = false;
    
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        renderer.render(source, state, full_opts);
    }
    end = high_resolution_clock::now();
    result.full_time_ms = duration<double, std::milli>(end - start).count() / iterations;
    
    // Calculate throughput
    size_t total_pixels = source.width() * source.height();
    result.pixels_per_second = total_pixels / (result.full_time_ms / 1000.0);
    
    // Estimate memory usage
    result.memory_bytes = source.dataSize() + total_pixels * 3 * sizeof(float);
    
    return result;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Photo Engine Benchmark Suite\n";
    std::cout << "========================================\n\n";
    
    // Test image sizes
    struct TestCase {
        std::string name;
        uint32_t width;
        uint32_t height;
    };
    
    std::vector<TestCase> test_cases = {
        {"HD (1920x1080)", 1920, 1080},
        {"4K (3840x2160)", 3840, 2160},
        {"12MP (4000x3000)", 4000, 3000},
        {"24MP (6000x4000)", 6000, 4000},  // Target: 24 megapixels
        {"50MP (8000x6250)", 8000, 6250},
    };
    
    // Adjustment states to test
    struct AdjustCase {
        std::string name;
        AdjustmentState state;
    };
    
    std::vector<AdjustCase> adjust_cases = {
        {"Identity", AdjustmentState{}},
        {"Exposure +1EV", [](){
            AdjustmentState s;
            s.exposure = 1.0f;
            return s;
        }()},
        {"Full Adjustments", [](){
            AdjustmentState s;
            s.exposure = 0.5f;
            s.contrast = 20.0f;
            s.highlights = -30.0f;
            s.shadows = 40.0f;
            s.whites = 10.0f;
            s.blacks = -5.0f;
            s.temperature = 5500.0f;
            s.tint = 10.0f;
            s.vibrance = 25.0f;
            s.saturation = 15.0f;
            s.tone_curves.rgb().addControlPoint(0.25f, 0.3f);
            s.tone_curves.rgb().addControlPoint(0.75f, 0.7f);
            return s;
        }()}
    };
    
    for (const auto& tc : test_cases) {
        std::cout << "\n----------------------------------------\n";
        std::cout << "Image: " << tc.name << " (" << tc.width << "x" << tc.height << ")\n";
        std::cout << "Total pixels: " << (tc.width * tc.height) / 1000000 << " MP\n";
        std::cout << "----------------------------------------\n\n";
        
        SourceImage source = generateTestImage(tc.width, tc.height);
        
        for (const auto& ac : adjust_cases) {
            std::cout << "Adjustment: " << ac.name << "\n";
            
            BenchmarkResult result = runBenchmark(source, ac.state, 3);
            
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  Preview render time: " << result.preview_time_ms << " ms\n";
            std::cout << "  Full render time:    " << result.full_time_ms << " ms\n";
            std::cout << "  Throughput:          " << (result.pixels_per_second / 1000000.0) << " MP/s\n";
            std::cout << "  Memory estimate:     " << (result.memory_bytes / 1024.0 / 1024.0) << " MB\n";
            std::cout << "\n";
        }
    }
    
    // Test caching effectiveness
    std::cout << "\n========================================\n";
    std::cout << "Cache Effectiveness Test\n";
    std::cout << "========================================\n\n";
    
    SourceImage cache_source = generateTestImage(4000, 3000);
    AdjustmentState cache_state;
    cache_state.exposure = 0.5f;
    
    CPURenderer renderer;
    
    RenderOptions opts;
    opts.use_cache = true;
    
    // First render (cache miss)
    auto start = high_resolution_clock::now();
    auto r1 = renderer.render(cache_source, cache_state, opts);
    auto end = high_resolution_clock::now();
    double first_time = duration<double, std::milli>(end - start).count();
    
    // Second render (cache hit)
    start = high_resolution_clock::now();
    auto r2 = renderer.render(cache_source, cache_state, opts);
    end = high_resolution_clock::now();
    double second_time = duration<double, std::milli>(end - start).count();
    
    std::cout << "First render (cache miss):  " << std::fixed << std::setprecision(2) << first_time << " ms\n";
    std::cout << "Second render (cache hit): " << std::fixed << std::setprecision(2) << second_time << " ms\n";
    std::cout << "Speedup:                   " << std::fixed << std::setprecision(1) << (first_time / second_time) << "x\n";
    std::cout << "Cache memory usage:        " << (renderer.getCache().getMemoryUsage() / 1024.0 / 1024.0) << " MB\n";
    
    std::cout << "\n========================================\n";
    std::cout << "Benchmark Complete\n";
    std::cout << "========================================\n";
    
    return 0;
}
