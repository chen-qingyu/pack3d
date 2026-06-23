#pragma once

namespace hypercube::config
{

// 全局
static constexpr double TIME_LIMIT = 120.0;
static constexpr int RANDOM_SEED = 42;

// GLC
static constexpr int GLC_WIDTH = 27;
static constexpr int GLC_KEEP_TOP_N_CAP_TINY = 4;
static constexpr int GLC_KEEP_TOP_N_CAP_NORMAL = 16;
static constexpr int GLC_MAX_REFINE_ROUNDS_TINY = 2;
static constexpr int GLC_MAX_REFINE_ROUNDS_NORMAL = 6;
static constexpr int GLC_MAX_EVAL_WIDTH_TINY = 6;
static constexpr int GLC_MAX_EVAL_WIDTH_NORMAL = 4;
static constexpr int GLC_EVAL_WIDTH = 4;

// RGS
static constexpr int RGS_MIN_TOTAL = 10;
static constexpr int RGS_MAX_TOTAL = 500;

} // namespace hypercube::config
