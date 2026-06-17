#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../constraints.hpp"
#include "../../geometry.hpp"
#include "../../types.hpp"
#include "placer.hpp"
#include "state.hpp"

namespace hypercube::sgep
{

// SGEP 简单贪心极点算法
class Packer
{
public:
    Packer(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, ContainerType>& container_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    [[nodiscard]] Solution pack();

private:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, ContainerType>& container_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;
    Placer placer_;

    SearchState make_initial_state() const;
    bool construct_solution(SearchState& state);
    bool open_new_container(SearchState& state);
    bool check_time(const SearchState& state) const;
    void update_best(SearchState& state);
    Solution build_solution(const SearchState& state, const std::string& status) const;
    bool check_tender_limit(SearchState& state);
};

} // namespace hypercube::sgep
