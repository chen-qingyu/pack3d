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
// 小实例（待装<=阈值）放宽束宽：箱数少时深搜跑得动，也更可能把最后几个箱子塞下。
// 大实例仍用 15，深搜会把整个时间预算烧光且收效甚微（br1-001：15→40 只多 3 箱，
// 耗时从 0.74s 涨到 30s）。
static constexpr int BSG_MAX_W_SMALL = 40;
static constexpr int BSG_SMALL_BOXES = 32;
static constexpr int BSG_SPACE_LIMIT = 12; // 多 cuboid 展开限界（按体积取大残差空间）

} // namespace pack3d::config
