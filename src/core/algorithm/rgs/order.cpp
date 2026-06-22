#include "order.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <set>
#include <unordered_set>

namespace hypercube::rgs
{

namespace
{

std::set<int32_t> reachable_z_heights(const BoxType& bt) noexcept
{
    std::set<int32_t> zs;
    for (auto o : bt.allowed_orientations)
    {
        auto os = bt.size.orient(o);
        zs.insert(os.dz);
    }
    return zs;
}

bool sets_intersect(const std::set<int32_t>& a, const std::set<int32_t>& b) noexcept
{
    if (a.empty() || b.empty())
    {
        return false;
    }
    auto ia = a.begin();
    auto ib = b.begin();
    while (ia != a.end() && ib != b.end())
    {
        if (*ia < *ib)
        {
            ++ia;
        }
        else if (*ib < *ia)
        {
            ++ib;
        }
        else
        {
            return true;
        }
    }
    return false;
}

struct IdenticalGroup
{
    std::string box_type_id;
    bool stackable = true;
    std::set<int32_t> z_heights;
    std::vector<const Box*> boxes;
    int64_t total_volume = 0;
    int64_t max_volume = 0;
};

struct SimilarGroup
{
    bool stackable = true;
    std::vector<IdenticalGroup> identicals;
    int64_t total_volume = 0;
    int64_t max_volume = 0;
};

int64_t box_volume(const BoxType& bt) noexcept
{
    return bt.size.volume();
}

} // anonymous namespace

std::vector<OrderEntry> build_ordered_list(
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho) noexcept
{
    if (remaining.empty())
    {
        return {};
    }

    // ---- step 1: build identical groups ----
    std::map<std::string, IdenticalGroup> ig_map;

    for (const auto& bx : remaining)
    {
        auto bt_it = box_type_map.find(bx.box_type_id);
        if (bt_it == box_type_map.end())
        {
            continue;
        }
        const auto& bt = bt_it->second;

        auto& ig = ig_map[bx.box_type_id];
        if (ig.boxes.empty())
        {
            ig.box_type_id = bx.box_type_id;
            ig.stackable = bt.stackable;
            ig.z_heights = reachable_z_heights(bt);
            ig.max_volume = box_volume(bt);
        }
        ig.boxes.push_back(&bx);
        ig.total_volume += box_volume(bt);
    }

    // ---- step 2: merge identical groups into similar groups ----
    std::vector<SimilarGroup> sgs;

    for (auto& [btid, ig] : ig_map)
    {
        bool merged = false;
        for (auto& sg : sgs)
        {
            if (sg.stackable == ig.stackable && sets_intersect(sg.identicals.front().z_heights, ig.z_heights))
            {
                sg.identicals.push_back(std::move(ig));
                sg.total_volume += sg.identicals.back().total_volume;
                sg.max_volume = std::max(sg.max_volume, sg.identicals.back().max_volume);
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            SimilarGroup sg;
            sg.stackable = ig.stackable;
            sg.total_volume = ig.total_volume;
            sg.max_volume = ig.max_volume;
            sg.identicals.push_back(std::move(ig));
            sgs.push_back(std::move(sg));
        }
    }

    // ---- step 3: sort similar groups by criterion ----
    auto sg_cmp = [criterion](const SimilarGroup& a, const SimilarGroup& b) -> bool
    {
        switch (criterion)
        {
            case SortCriterion::StackabilityCumulatedVolume:
                if (a.stackable != b.stackable)
                {
                    return a.stackable > b.stackable;
                }
                return a.total_volume > b.total_volume;

            case SortCriterion::StackabilityHighestVolume:
                if (a.stackable != b.stackable)
                {
                    return a.stackable > b.stackable;
                }
                return a.max_volume > b.max_volume;

            case SortCriterion::CumulatedVolume:
                return a.total_volume > b.total_volume;

            case SortCriterion::HighestVolume:
                return a.max_volume > b.max_volume;

            case SortCriterion::Random:
            default:
                return false;
        }
    };

    if (criterion == SortCriterion::Random)
    {
        std::mt19937 rng(42);
        std::shuffle(sgs.begin(), sgs.end(), rng);
    }
    else
    {
        std::sort(sgs.begin(), sgs.end(), sg_cmp);
    }

    // ---- step 4: within each similar group, sort identical groups by volume desc ----
    for (auto& sg : sgs)
    {
        std::sort(sg.identicals.begin(), sg.identicals.end(),
                  [](const IdenticalGroup& a, const IdenticalGroup& b)
                  { return a.max_volume > b.max_volume; });
    }

    // ---- step 5: flatten to OrderEntry list ----
    std::vector<OrderEntry> ordered;

    for (const auto& sg : sgs)
    {
        for (const auto& ig : sg.identicals)
        {
            auto bt_it = box_type_map.find(ig.box_type_id);
            if (bt_it == box_type_map.end())
            {
                continue;
            }
            const auto& orients = bt_it->second.allowed_orientations;

            for (const auto* bx : ig.boxes)
            {
                ordered.push_back({bx->id, orients});
            }
        }
    }

    // ---- step 6: Shaw randomization with ρ ----
    if (rho > 0.0)
    {
        static std::atomic<uint64_t> s_call_id{0};
        uint64_t call_id = s_call_id.fetch_add(1);

        std::mt19937_64 rng(42 + call_id);
        for (auto& entry : ordered)
        {
            if (entry.orients.size() > 1)
            {
                std::shuffle(entry.orients.begin(), entry.orients.end(), rng);
            }
        }

        if (ordered.size() > 1)
        {
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            size_t m = ordered.size();
            std::vector<OrderEntry> shuffled;
            shuffled.reserve(m);
            auto work = ordered;

            for (size_t j = 0; j < m && !work.empty(); ++j)
            {
                double y = dist(rng);
                size_t remaining_count = work.size();
                size_t pick = static_cast<size_t>(std::ceil(std::pow(y, 1.0 / rho) * remaining_count));
                if (pick == 0)
                {
                    pick = 1;
                }
                if (pick > remaining_count)
                {
                    pick = remaining_count;
                }

                shuffled.push_back(work[pick - 1]);
                work.erase(work.begin() + static_cast<ptrdiff_t>(pick - 1));
            }

            ordered = std::move(shuffled);
        }
    }

    return ordered;
}

} // namespace hypercube::rgs
