#pragma once

#include <map>
#include <string>
#include <vector>

#include "types.hpp"

namespace pack3d
{

// 求解器引擎
class SolverEngine
{
public:
    explicit SolverEngine(const Problem& problem);

    /// 运行求解器，返回最佳解
    [[nodiscard]] Solution solve();

private:
    Problem problem_;
    std::map<std::string, BoxType> box_type_map_;
    std::map<std::string, Box> box_map_;
    bool has_weight_info_ = false;
};

} // namespace pack3d
