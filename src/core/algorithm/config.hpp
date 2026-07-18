#pragma once

namespace pack3d::config
{

// 全局
static constexpr double TIME_LIMIT = 120.0;
static constexpr int RANDOM_SEED = 42;

// GLC
static constexpr int GLC_WIDTH = 9;
static constexpr int GLC_KEEP_TOP_N_CAP = 4;
static constexpr int GLC_MAX_REFINE_ROUNDS = 2;
static constexpr int GLC_EVAL_WIDTH = 4;

// RGS
static constexpr int RGS_MIN_TOTAL = 10;
static constexpr int RGS_MAX_TOTAL = 500;

// BSG
static constexpr int BSG_MAX_BL = 10000;
static constexpr double BSG_MAX_FR_WEAK = 1.00;   // box_types < 阈值
static constexpr double BSG_MAX_FR_STRONG = 0.98; // box_types >= 阈值
static constexpr int BSG_THRESHOLD_BOX_TYPES = 30;
static constexpr int BSG_MAX_W = 15;

} // namespace pack3d::config
