/**
 * Unit tests for DSP ENGINE v0.4.0
 */

#include "test_framework.h"
#include "dsp_engine.h"
#include "audio/ring_buffer.h"
#include "utils/math_utils.h"
#include "core/resampler.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdint>

using namespace dsp_test;

static DspEngine* create_engine(int channels = 2, float rate = 48000.f) {
    DspConfig cfg{};
    cfg.sample_rate = rate;
    cfg.channels = channels;
    cfg.max_block = 1024;
    cfg.enable_eq = 1;
    cfg.enable_dvc = 1;
    cfg.enable_limiter = 1;
    cfg.enable_dither = 0;
    cfg.dither_mode = DSP_DITHER_TPDF;
    cfg.limiter_threshold_db = -1.0f;
    cfg.limiter_release_ms = 50.f;
    return dsp_create(&cfg);
}

TEST(version) {
    const char* v = dsp_version();
    EXPECT_TRUE(v != nullptr);
    EXPECT_TRUE(std::strstr(v, "0.4") != nullptr);
}

TEST(lifecycle) {
    DspEngine* eng = create_engine();
    EXPECT_TRUE(eng != nullptr);
    EXPECT_EQ(dsp_reset(eng), DSP_OK);
    dsp_destroy(eng);
}

TEST(invalid_create) {
    DspConfig cfg{};
    cfg.channels = 0;
    cfg.max_block = 1024;
    EXPECT_TRUE(dsp_create(&cfg) == nullptr);
}

// remaining tests loaded from local - see follow-up if truncated
