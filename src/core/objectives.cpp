#include "objectives.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hypercube
{

bool ObjectiveVector::is_better_than(const ObjectiveVector& rhs) const noexcept
{
    return compare_objectives(*this, rhs, default_objective_keys()) < 0;
}

bool ObjectiveVector::is_better_than(const ObjectiveVector& rhs, const std::vector<std::string>& keys) const noexcept
{
    return compare_objectives(*this, rhs, keys) < 0;
}

bool ObjectiveVector::operator==(const ObjectiveVector& rhs) const noexcept
{
    return container_count == rhs.container_count &&
           platform_count == rhs.platform_count &&
           std::abs(avg_volume_rate - rhs.avg_volume_rate) < 1e-12 &&
           group_split_sum == rhs.group_split_sum;
}

ObjectiveVector compute_objective(const std::vector<ContainerLoad>& containers) noexcept
{
    ObjectiveVector ov;
    ov.container_count = static_cast<int>(containers.size());

    double sum_rate = 0.0;

    std::map<std::string, std::set<std::string>> group_containers;

    for (const auto& c : containers)
    {
        ov.platform_count += static_cast<int>(c.platforms.size());
        sum_rate += c.volume_rate();

        for (const auto& g : c.groups)
        {
            group_containers[g].insert(c.instance_id);
        }
    }

    ov.avg_volume_rate = ov.container_count > 0
                             ? sum_rate / ov.container_count
                             : 0.0;

    int split_sum = 0;
    for (const auto& [g, containers_set] : group_containers)
    {
        split_sum += static_cast<int>(containers_set.size());
    }
    ov.group_split_sum = split_sum;

    return ov;
}

int compare_objectives(const ObjectiveVector& a,
                       const ObjectiveVector& b,
                       const std::vector<std::string>& keys) noexcept
{
    for (const auto& key : keys)
    {
        if (key == "min_container_count")
        {
            if (a.container_count < b.container_count)
            {
                return -1;
            }
            if (a.container_count > b.container_count)
            {
                return 1;
            }
        }
        else if (key == "min_platform_count")
        {
            if (a.platform_count < b.platform_count)
            {
                return -1;
            }
            if (a.platform_count > b.platform_count)
            {
                return 1;
            }
        }
        else if (key == "max_volume_rate")
        {
            if (a.avg_volume_rate > b.avg_volume_rate + 1e-12)
            {
                return -1;
            }
            if (b.avg_volume_rate > a.avg_volume_rate + 1e-12)
            {
                return 1;
            }
        }
        else if (key == "min_group_split")
        {
            if (a.group_split_sum < b.group_split_sum)
            {
                return -1;
            }
            if (a.group_split_sum > b.group_split_sum)
            {
                return 1;
            }
        }
    }
    return 0; // 相等
}

} // namespace hypercube
