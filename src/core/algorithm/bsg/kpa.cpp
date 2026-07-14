#include "kpa.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace pack3d::bsg
{

namespace
{

// 对单个轴跑多选背包 DP，返回前缀 max 后的 dp 数组。
// 每件箱子至多选择一个允许朝向，避免同一箱子在多个朝向中被重复使用。
std::vector<int> knapsack_axis(
    int cap,
    const std::vector<int>& remaining_counts,
    const std::vector<BoxType>& box_types,
    int (*dim_fn)(const OrientedSize&))
{
    std::vector<int> dp(cap + 1, 0);

    for (size_t ti = 0; ti < box_types.size(); ++ti)
    {
        int count = remaining_counts[ti];
        if (count <= 0)
        {
            continue;
        }

        const auto& bt = box_types[ti];

        std::vector<int> lengths;
        for (auto orient : bt.allowed_orientations)
        {
            int len = dim_fn(bt.size.orient(orient));
            if (len > 0 && len <= cap)
            {
                lengths.push_back(len);
            }
        }
        std::sort(lengths.begin(), lengths.end());
        lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());
        if (lengths.empty())
        {
            continue;
        }

        for (int copy = 0; copy < count; ++copy)
        {
            std::vector<int> next = dp;
            for (int c = 0; c <= cap; ++c)
            {
                for (int len : lengths)
                {
                    if (c + len <= cap)
                    {
                        next[c + len] = std::max(next[c + len], dp[c] + len);
                    }
                }
            }
            dp = std::move(next);
        }
    }

    // 前缀 max
    for (int c = 1; c <= cap; ++c)
    {
        dp[c] = std::max(dp[c], dp[c - 1]);
    }
    return dp;
}

int dim_dx(const OrientedSize& os)
{
    return os.dx;
}
int dim_dy(const OrientedSize& os)
{
    return os.dy;
}
int dim_dz(const OrientedSize& os)
{
    return os.dz;
}

} // namespace

void run_kpa(BSGState& state, const GlobalContext& ctx)
{
    if (state.kpa_L.has_value())
    {
        return;
    } // already computed

    int L = ctx.container_size.x;
    int W = ctx.container_size.y;
    int H = ctx.container_size.z;

    state.kpa_L = knapsack_axis(L, state.remaining_counts, ctx.box_types, dim_dx);
    state.kpa_W = knapsack_axis(W, state.remaining_counts, ctx.box_types, dim_dy);
    state.kpa_H = knapsack_axis(H, state.remaining_counts, ctx.box_types, dim_dz);
}

int64_t compute_v_loss(const BSGState& state, const Cuboid& r,
                       const GeneralBlock& b, const GlobalContext& ctx)
{
    (void)ctx;
    assert(state.kpa_L.has_value() && state.kpa_W.has_value() && state.kpa_H.has_value());

    int l_rest = r.lx - b.osize.dx - 1; // strict < per paper
    int w_rest = r.ly - b.osize.dy - 1;
    int h_rest = r.lz - b.osize.dz - 1;

    // clamp to valid range
    int cap_L = static_cast<int>(state.kpa_L->size()) - 1;
    int cap_W = static_cast<int>(state.kpa_W->size()) - 1;
    int cap_H = static_cast<int>(state.kpa_H->size()) - 1;

    int l_max = (l_rest >= 0 && l_rest <= cap_L) ? (*state.kpa_L)[l_rest] : 0;
    int w_max = (w_rest >= 0 && w_rest <= cap_W) ? (*state.kpa_W)[w_rest] : 0;
    int h_max = (h_rest >= 0 && h_rest <= cap_H) ? (*state.kpa_H)[h_rest] : 0;

    int64_t fill_l = static_cast<int64_t>(b.osize.dx) + l_max;
    int64_t fill_w = static_cast<int64_t>(b.osize.dy) + w_max;
    int64_t fill_h = static_cast<int64_t>(b.osize.dz) + h_max;
    int64_t fill_vol = fill_l * fill_w * fill_h;

    return r.volume() - fill_vol;
}

int64_t compute_f(const BSGState& state, const Cuboid& r,
                  const GeneralBlock& b, const GlobalContext& ctx)
{
    // 块放不下：返回负无穷
    if (b.osize.dx > r.lx || b.osize.dy > r.ly || b.osize.dz > r.lz)
    {
        return std::numeric_limits<int64_t>::lowest();
    }
    return b.single_box_volume - compute_v_loss(state, r, b, ctx);
}

} // namespace pack3d::bsg
