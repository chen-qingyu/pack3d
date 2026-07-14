#include "beam.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "../../tool.hpp"
#include "expand.hpp"
#include "greedy.hpp"
#include "kpa.hpp"

namespace pack3d::bsg
{

namespace
{

// FNV-1a 64-bit hash for a vector of ints (packed_counts)
uint64_t hash_counts(const std::vector<int>& counts) noexcept
{
    constexpr uint64_t prime = 0x100000001b3ULL;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int c : counts)
    {
        h = (h ^ static_cast<uint64_t>(c)) * prime;
    }
    return h;
}

} // namespace

int64_t beam_search(
    BSGState s0,
    int w,
    int64_t& s_best_volume,
    BSGState& s_best,
    const GlobalContext& ctx)
{
    // 初始 KPA（根节点）
    run_kpa(s0, ctx);

    std::vector<BSGState> S;
    S.push_back(std::move(s0));

    int64_t best_in_round = 0;
    bool is_first_layer = true;

    while (!S.empty())
    {
        // 每层开始检查超时
        if (!TimeChecker::check())
        {
            break;
        }

        std::vector<BSGState> S_prime;

        for (auto& s : S)
        {
            // 确保 KPA 已计算
            run_kpa(s, ctx);

            // 首层用 min(w², 可用块数)，避免无效扩张和 int 溢出。
            int ew;
            if (is_first_layer)
            {
                int candidate_count = static_cast<int>(s.available_blocks.size());
                if (w >= candidate_count || w > candidate_count / w)
                {
                    ew = candidate_count;
                }
                else
                {
                    ew = w * w;
                }
            }
            else
            {
                ew = w;
            }
            auto succ = expand(s, ew, ctx);
            for (auto& ss : succ)
            {
                S_prime.push_back(std::move(ss));
            }
        }

        if (S_prime.empty())
        {
            break;
        }

        // ---- 去相似状态 ----
        // 对每个 S' 中的 state 跑 greedy rollout，同时做去重
        struct Slot
        {
            int64_t score = 0;
            size_t idx = 0;
            bool keep = true;
        };
        std::vector<Slot> slots(S_prime.size());
        std::unordered_map<uint64_t, size_t> seen; // hash → slot index

        for (size_t i = 0; i < S_prime.size(); ++i)
        {
            auto gr = greedy_rollout(S_prime[i], s_best_volume, ctx);
            int64_t score = gr.total_volume;

            // 去相似：相同 packed_counts 只保留 used_volume 更小的
            uint64_t h = hash_counts(gr.packed_counts);
            auto it = seen.find(h);
            if (it != seen.end())
            {
                size_t j = it->second;
                if (S_prime[i].used_volume < S_prime[j].used_volume)
                {
                    // 当前 state 已装更少 → 保留当前，淘汰旧的
                    slots[j].keep = false;
                    it->second = i;
                }
                else
                {
                    // 旧的更好 → 淘汰当前
                    slots[i].keep = false;
                }
            }
            else
            {
                seen[h] = i;
            }

            slots[i].score = score;
            slots[i].idx = i;

            // 更新全局最优
            if (score > s_best_volume)
            {
                s_best_volume = score;
                s_best = std::move(gr.final_state);
            }
        }

        // 过滤：保留 keep=true 的
        std::vector<Slot> kept;
        for (auto& sl : slots)
        {
            if (sl.keep)
            {
                kept.push_back(sl);
            }
        }

        // 按 greedy score 降序，取 top-w
        int take = std::min(w, static_cast<int>(kept.size()));
        if (take <= 0)
        {
            // 全部被去重淘汰了 → 保留原 slots 中 score 最高的 w 个
            take = std::min(w, static_cast<int>(slots.size()));
            std::partial_sort(slots.begin(), slots.begin() + take, slots.end(),
                              [](const Slot& a, const Slot& b) noexcept
                              {
                                  return a.score > b.score;
                              });
            S.clear();
            for (int i = 0; i < take; ++i)
            {
                if (slots[i].score > best_in_round)
                {
                    best_in_round = slots[i].score;
                }
                S.push_back(std::move(S_prime[slots[i].idx]));
            }
        }
        else
        {
            std::partial_sort(kept.begin(), kept.begin() + take, kept.end(),
                              [](const Slot& a, const Slot& b) noexcept
                              {
                                  return a.score > b.score;
                              });
            S.clear();
            for (int i = 0; i < take; ++i)
            {
                if (kept[i].score > best_in_round)
                {
                    best_in_round = kept[i].score;
                }
                S.push_back(std::move(S_prime[kept[i].idx]));
            }
        }

        is_first_layer = false;
    }

    return best_in_round;
}

} // namespace pack3d::bsg
