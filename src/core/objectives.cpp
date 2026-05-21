#include "objectives.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hypercube
{

// =============================================================
// resolve_objective_keys
// =============================================================
std::vector<std::string> resolve_objective_keys(
    const std::vector<std::string>& user_keys) noexcept
{
    if (user_keys.empty())
    {
        return default_objective_keys();
    }
    return user_keys;
}

// =============================================================
// ObjectiveVector::is_better_than
// =============================================================
bool ObjectiveVector::is_better_than(const ObjectiveVector& rhs) const noexcept
{
    return compare_objectives(*this, rhs) < 0;
}

bool ObjectiveVector::operator==(const ObjectiveVector& rhs) const noexcept
{
    return container_count == rhs.container_count &&
           total_platforms == rhs.total_platforms &&
           std::abs(avg_volume_rate - rhs.avg_volume_rate) < 1e-12 &&
           group_split_sum == rhs.group_split_sum;
}

// =============================================================
// compute_objective
// =============================================================
ObjectiveVector compute_objective(
    const std::vector<ContainerLoad>& containers) noexcept
{
    ObjectiveVector ov;
    ov.container_count = static_cast<int>(containers.size());

    int total_plat = 0;
    double sum_rate = 0.0;
    int container_count_for_rate = 0;

    // 组分散追踪
    std::map<std::string, std::set<std::string>> group_containers;

    for (const auto& c : containers)
    {
        total_plat += static_cast<int>(c.platforms.size());

        if (c.type)
        {
            sum_rate += c.volume_rate();
            ++container_count_for_rate;
        }

        for (const auto& g : c.groups)
        {
            group_containers[g].insert(c.instance_id);
        }
    }

    ov.total_platforms = total_plat;
    ov.avg_volume_rate = container_count_for_rate > 0
                             ? sum_rate / container_count_for_rate
                             : 0.0;

    int split_sum = 0;
    for (const auto& [g, containers_set] : group_containers)
    {
        split_sum += static_cast<int>(containers_set.size());
    }
    ov.group_split_sum = split_sum;

    return ov;
}

// =============================================================
// project_objective — 增量投影
// =============================================================
ObjectiveVector project_objective(
    const ObjectiveVector& current,
    const ContainerLoad& target_container,
    bool is_new_container,
    bool is_new_platform,
    const std::string& box_group,
    bool group_touches_new_container) noexcept
{
    ObjectiveVector projected = current;

    if (is_new_container)
    {
        projected.container_count += 1;
    }

    if (is_new_platform)
    {
        projected.total_platforms += 1;
    }

    // avg_volume_rate 在没有完整重算的情况下难以精确投影
    // 局部比较时保留粗略估计，最终解时做完整重算

    if (group_touches_new_container && !box_group.empty())
    {
        projected.group_split_sum += 1;
    }

    return projected;
}

// =============================================================
// compare_objectives — 字典序比较
// =============================================================
int compare_objectives(const ObjectiveVector& a,
                       const ObjectiveVector& b) noexcept
{
    // v1: min_container_count（越小越好）
    if (a.container_count < b.container_count)
    {
        return -1;
    }
    if (a.container_count > b.container_count)
    {
        return 1;
    }

    // v2: min_platforms_per_container -> total_platforms（越小越好）
    if (a.total_platforms < b.total_platforms)
    {
        return -1;
    }
    if (a.total_platforms > b.total_platforms)
    {
        return 1;
    }

    // v3: max_avg_volume_rate（越大越好）
    if (a.avg_volume_rate > b.avg_volume_rate + 1e-12)
    {
        return -1;
    }
    if (b.avg_volume_rate > a.avg_volume_rate + 1e-12)
    {
        return 1;
    }

    // v4: min_group_split（越小越好）
    if (a.group_split_sum < b.group_split_sum)
    {
        return -1;
    }
    if (a.group_split_sum > b.group_split_sum)
    {
        return 1;
    }

    return 0; // 相等
}

} // namespace hypercube
