#pragma once

#include <map>
#include <string>
#include <vector>

#include "types.hpp"

namespace pack3d
{

class PackerBase;

void postprocess(std::vector<ContainerLoad>& all_loads,
                 PackerBase& packer,
                 const std::vector<ContainerType>& container_types,
                 const std::map<std::string, BoxType>& box_type_map,
                 const std::map<std::string, Box>& box_map);

} // namespace pack3d
