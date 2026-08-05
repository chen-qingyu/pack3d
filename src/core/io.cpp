#include "io.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json-schema.hpp>
#include <spdlog/spdlog.h>

#include "algorithm/config.hpp"
#include "constraints.hpp"
#include "input_schema.h"

namespace pack3d
{

namespace
{

// enum_name 输出大写（如 "XYZ"），项目约定小写（"xyz"）
std::string enum_to_lower(auto val) noexcept
{
    auto name = std::string{magic_enum::enum_name(val)};
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return name;
}

} // namespace

std::string orientation_to_string(Orientation o) noexcept
{
    return enum_to_lower(o);
}

Orientation orientation_from_string(const std::string& s)
{
    // schema 已严格枚举约束；非法值直接抛异常，由 run() 兜底返回 invalid
    return magic_enum::enum_cast<Orientation>(s, magic_enum::case_insensitive).value();
}

// 有值则序列化，无值则输出 null
template <typename T>
json opt_json(const std::optional<T>& opt) noexcept
{
    return opt.has_value() ? json(opt.value()) : json(nullptr);
}

// 从 JSON 中获取可选的 int，null 视为空 optional
std::optional<int> json_opt_int(const json& j, const char* key)
{
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
    {
        return std::nullopt;
    }
    return it->get<int>();
}

// 从 JSON 中获取可选的 double
std::optional<double> json_opt_double(const json& j, const char* key)
{
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
    {
        return std::nullopt;
    }
    return it->get<double>();
}

// 解析 标量 或 数组 为与 allowed_orientations 对齐的 optional 向量。
// 标量广播到全部朝向；数组按原样存储（长度对齐在 pre_validate 校验）。
template <typename T>
void parse_optional_vector(const json& j, const char* key,
                           std::vector<std::optional<T>>& out,
                           size_t align_size)
{
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
    {
        return;
    }
    if (it->is_array())
    {
        out.resize(it->size());
        for (size_t i = 0; i < it->size(); ++i)
        {
            if (!(*it)[i].is_null())
            {
                out[i] = (*it)[i].get<T>();
            }
        }
    }
    else
    {
        T v = it->get<T>();
        out.assign(align_size, std::optional<T>(v));
    }
}

void from_json(const json& j, ContainerType& ct)
{
    j["id"].get_to(ct.id);
    j["inner_size"]["x"].get_to(ct.inner_size.x);
    j["inner_size"]["y"].get_to(ct.inner_size.y);
    j["inner_size"]["z"].get_to(ct.inner_size.z);
    ct.max_weight = json_opt_double(j, "max_weight");
    ct.quantity_limit = json_opt_int(j, "quantity_limit");
}

void from_json(const json& j, BoxType& bt)
{
    j["id"].get_to(bt.id);
    j["size"]["x"].get_to(bt.size.x);
    j["size"]["y"].get_to(bt.size.y);
    j["size"]["z"].get_to(bt.size.z);
    for (const auto& o_str : j["allowed_orientations"])
    {
        bt.allowed_orientations.push_back(orientation_from_string(o_str.get<std::string>()));
    }
    parse_optional_vector<int>(j, "max_stack", bt.max_stack, bt.allowed_orientations.size());
    parse_optional_vector<double>(j, "max_load", bt.max_load, bt.allowed_orientations.size());
}

void from_json(const json& j, Box& bx)
{
    j["id"].get_to(bx.id);
    j["box_type_id"].get_to(bx.box_type_id);
    bx.weight = json_opt_double(j, "weight");
    bx.group = j.value("group", std::string());
    bx.platform = j.value("platform", std::string());
}

void from_json(const json& j, ExistingPlacement& ep)
{
    j["box_id"].get_to(ep.box_id);
    j["box_type_id"].get_to(ep.box_type_id);
    j["position"]["x"].get_to(ep.position.x);
    j["position"]["y"].get_to(ep.position.y);
    j["position"]["z"].get_to(ep.position.z);
    ep.orientation = orientation_from_string(j["orientation"].get<std::string>());
    ep.weight = json_opt_double(j, "weight");
    ep.platform = j.value("platform", std::string());
    ep.group = j.value("group", std::string());
    if (j.contains("size"))
    {
        OrientedSize sz;
        j["size"]["dx"].get_to(sz.dx);
        j["size"]["dy"].get_to(sz.dy);
        j["size"]["dz"].get_to(sz.dz);
        ep.size = sz;
    }
}

void from_json(const json& j, ExistingContainer& ec)
{
    j["type_id"].get_to(ec.type_id);
    for (const auto& item : j["placements"])
    {
        ec.placements.push_back(item.get<ExistingPlacement>());
    }
}

void from_json(const json& j, Problem& p)
{
    for (const auto& item : j["container_types"])
    {
        p.container_types.push_back(item.get<ContainerType>());
    }
    for (const auto& item : j["box_types"])
    {
        p.box_types.push_back(item.get<BoxType>());
    }
    // 承重约束启用标志（presence-based）
    for (const auto& bt : p.box_types)
    {
        for (const auto& v : bt.max_stack)
        {
            p.has_max_stack |= v.has_value();
        }
        for (const auto& v : bt.max_load)
        {
            p.has_max_load |= v.has_value();
        }
    }
    for (const auto& item : j["boxes"])
    {
        p.boxes.push_back(item.get<Box>());
    }

    p.time_limit = config::TIME_LIMIT;
    if (j.contains("constraints"))
    {
        const auto& c = j["constraints"];
        p.time_limit = c.value("time_limit", config::TIME_LIMIT);
        p.support_rate = c.value("support_rate", 0.0);
        p.platform_limit = json_opt_int(c, "platform_limit");
        p.tender_limit = json_opt_int(c, "tender_limit");
    }

    if (j.contains("route"))
    {
        RouteOrder route;
        for (const auto& plat : j["route"])
        {
            std::string pname = plat.get<std::string>();
            route.index_of[pname] = route.platform_order.size();
            route.platform_order.push_back(std::move(pname));
        }
        p.route = std::move(route);
    }

    if (j.contains("algorithm"))
    {
        p.algorithm = algorithm_from_string(j["algorithm"].get<std::string>());
    }

    if (j.contains("existing_containers"))
    {
        for (const auto& item : j["existing_containers"])
        {
            p.existing_containers.push_back(item.get<ExistingContainer>());
        }
    }
}

std::string algorithm_to_string(Algorithm a) noexcept
{
    return enum_to_lower(a);
}

Algorithm algorithm_from_string(const std::string& s)
{
    // schema 已严格枚举约束；非法值直接抛异常，由 run() 兜底返回 invalid
    return magic_enum::enum_cast<Algorithm>(s, magic_enum::case_insensitive).value();
}

std::string status_to_string(SolveStatus s) noexcept
{
    return enum_to_lower(s);
}

// 使用编译时嵌入的 schema 校验 JSON
std::vector<std::string> validate_schema(const json& j) noexcept
{
    std::vector<std::string> out;
    try
    {
        auto schema = json::parse(std::string{pack3d::INPUT_SCHEMA});
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(schema);
        validator.validate(j);
    }
    catch (const std::exception& e)
    {
        out.push_back(std::string("schema validation failed: ") + e.what());
    }
    return out;
}

std::vector<std::string> pre_validate_input(const Problem& problem) noexcept
{
    std::vector<std::string> out;

    // 重复 ID
    {
        std::set<std::string> seen;
        for (const auto& ct : problem.container_types)
        {
            if (!seen.insert(ct.id).second)
            {
                out.push_back("duplicate container_type id: " + ct.id);
            }
        }
    }
    {
        std::set<std::string> seen;
        for (const auto& bt : problem.box_types)
        {
            if (!seen.insert(bt.id).second)
            {
                out.push_back("duplicate box_type id: " + bt.id);
            }
        }
    }
    {
        std::set<std::string> seen;
        for (const auto& bx : problem.boxes)
        {
            if (!seen.insert(bx.id).second)
            {
                out.push_back("duplicate box id: " + bx.id);
            }
        }
    }

    std::set<std::string> bt_ids;
    for (const auto& bt : problem.box_types)
    {
        bt_ids.insert(bt.id);
    }
    for (const auto& bx : problem.boxes)
    {
        if (!bt_ids.count(bx.box_type_id))
        {
            out.push_back("unknown box_type_id '" + bx.box_type_id + "' for box " + bx.id);
        }
    }

    // 重量信息一致性校验
    bool any_box_has_weight = false;
    bool all_boxes_have_weight = true;
    for (const auto& bx : problem.boxes)
    {
        if (bx.weight.has_value())
        {
            any_box_has_weight = true;
        }
        else
        {
            all_boxes_have_weight = false;
        }
    }
    if (any_box_has_weight)
    {
        if (!all_boxes_have_weight)
        {
            out.push_back("inconsistent weight: some boxes have weight, some don't");
        }
        for (const auto& ct : problem.container_types)
        {
            if (!ct.max_weight.has_value())
            {
                out.push_back("inconsistent weight: container_type " + ct.id + " missing max_weight");
            }
        }
    }

    // 承重约束：max_stack/max_load 数组长度与 allowed_orientations 对齐
    bool any_max_stack = false;
    bool any_max_load = false;
    for (const auto& bt : problem.box_types)
    {
        for (const auto& v : bt.max_stack)
        {
            any_max_stack |= v.has_value();
        }
        for (const auto& v : bt.max_load)
        {
            any_max_load |= v.has_value();
        }
        if (!bt.max_stack.empty() && bt.max_stack.size() != bt.allowed_orientations.size())
        {
            out.push_back("box_type " + bt.id + ": max_stack array length " +
                          std::to_string(bt.max_stack.size()) + " != allowed_orientations length " +
                          std::to_string(bt.allowed_orientations.size()));
        }
        if (!bt.max_load.empty() && bt.max_load.size() != bt.allowed_orientations.size())
        {
            out.push_back("box_type " + bt.id + ": max_load array length " +
                          std::to_string(bt.max_load.size()) + " != allowed_orientations length " +
                          std::to_string(bt.allowed_orientations.size()));
        }
    }
    if (any_max_load && !all_boxes_have_weight)
    {
        out.push_back("inconsistent weight: max_load requires all boxes to have weight");
    }

    // 箱子/已有放置有 platform 就必须有 route 且平台在路线中
    bool any_platform = false;
    for (const auto& bx : problem.boxes)
    {
        any_platform |= !bx.platform.empty();
    }
    for (const auto& ec : problem.existing_containers)
    {
        for (const auto& ep : ec.placements)
        {
            any_platform |= !ep.platform.empty();
        }
    }
    if (any_platform && !problem.route.has_value())
    {
        out.push_back("platform set but route is missing");
    }

    if (problem.route.has_value())
    {
        const auto& route = problem.route.value();

        std::set<std::string> rseen;
        for (const auto& p : route.platform_order)
        {
            if (!rseen.insert(p).second)
            {
                out.push_back("duplicate platform in route: " + p);
            }
        }

        for (const auto& bx : problem.boxes)
        {
            if (!bx.platform.empty() && !route.index_of.count(bx.platform))
            {
                out.push_back("platform '" + bx.platform + "' not in route, box " + bx.id);
            }
        }

        for (size_t ei = 0; ei < problem.existing_containers.size(); ++ei)
        {
            const auto& ec = problem.existing_containers[ei];
            for (size_t pi = 0; pi < ec.placements.size(); ++pi)
            {
                const auto& ep = ec.placements[pi];
                if (!ep.platform.empty() && !route.index_of.count(ep.platform))
                {
                    out.push_back("platform '" + ep.platform + "' not in route, existing_containers[" +
                                  std::to_string(ei) + "].placements[" + std::to_string(pi) +
                                  "] (box " + ep.box_id + ")");
                }
            }
        }
    }

    // 已有容器校验
    if (!problem.existing_containers.empty())
    {
        std::map<std::string, const ContainerType*> ct_map;
        for (const auto& ct : problem.container_types)
        {
            ct_map[ct.id] = &ct;
        }
        std::map<std::string, BoxType> bt_map;
        for (const auto& bt : problem.box_types)
        {
            bt_map[bt.id] = bt;
        }

        std::set<std::string> placed_ids;
        std::map<std::string, int> ct_usage;

        for (size_t ei = 0; ei < problem.existing_containers.size(); ++ei)
        {
            const auto& ec = problem.existing_containers[ei];
            std::string prefix = "existing_containers[" + std::to_string(ei) + "]";

            if (!ct_map.count(ec.type_id))
            {
                out.push_back(prefix + ": unknown container type_id '" + ec.type_id + "'");
                continue;
            }
            ct_usage[ec.type_id]++;

            std::vector<std::string> build_errors;
            auto load = build_load_from_existing(ec, ct_map, bt_map, build_errors);
            for (auto& e : build_errors)
            {
                out.push_back(prefix + ": " + e);
            }
            if (!build_errors.empty())
            {
                continue;
            }

            const auto& ct = *ct_map[ec.type_id];

            // 堆码层数 / 单箱承重校验（重建堆叠状态并检查）
            if (any_max_stack || any_max_load)
            {
                std::vector<std::string> stack_errs;
                recompute_stack_state(load, bt_map, &stack_errs);
                for (const auto& e : stack_errs)
                {
                    out.push_back(prefix + ": " + e);
                }
            }

            // 检查容器数量限制
            if (ct.quantity_limit.has_value() && ct_usage[ec.type_id] > ct.quantity_limit.value())
            {
                out.push_back(prefix + ": exceeds quantity_limit for container type " + ec.type_id);
            }

            for (size_t pi = 0; pi < load.placements.size(); ++pi)
            {
                const auto& pl = load.placements[pi];
                std::string pfx = prefix + ".placements[" + std::to_string(pi) + "]";

                if (!placed_ids.insert(pl.box_id).second)
                {
                    out.push_back(pfx + ": duplicate box_id '" + pl.box_id + "'");
                }

                if (!check_boundary(ct, pl.position, pl.osize))
                {
                    out.push_back(pfx + " (" + pl.box_id + "): out of container boundary");
                }

                // 只检查与已校验放置的重叠
                for (size_t pj = 0; pj < pi; ++pj)
                {
                    const auto& prev = load.placements[pj];
                    if (check_overlap(pl.position, pl.osize, {prev}))
                    {
                        out.push_back(pfx + " (" + pl.box_id + "): overlaps with " + prev.box_id);
                    }
                }

                if (!check_support(pl.position, pl.osize, load, problem.support_rate))
                {
                    out.push_back(pfx + " (" + pl.box_id + "): insufficient support");
                }

                if (problem.platform_limit.has_value() && !pl.platform.empty())
                {
                    // 检查当前放置加入后的平台数
                    auto test_load = load;
                    if (!check_platform_limit(test_load, pl.platform, problem.platform_limit.value()))
                    {
                        out.push_back(pfx + " (" + pl.box_id + "): exceeds platform_limit");
                    }
                }

                if (problem.route.has_value() && !pl.platform.empty())
                {
                    if (!check_route_order(load, pl.platform, pl.position, pl.osize, problem.route.value()))
                    {
                        out.push_back(pfx + " (" + pl.box_id + "): violates route order");
                    }
                }
            }

            // 校验重量
            if (ct.max_weight.has_value() && load.total_weight > ct.max_weight.value() + 1e-9)
            {
                out.push_back(prefix + ": total weight " + std::to_string(load.total_weight) +
                              " exceeds max_weight " + std::to_string(ct.max_weight.value()));
            }
        }
    }

    return out;
}

ContainerLoad build_load_from_existing(
    const ExistingContainer& ec,
    const std::map<std::string, const ContainerType*>& ct_map,
    const std::map<std::string, BoxType>& bt_map,
    std::vector<std::string>& errors)
{
    ContainerLoad load;
    auto ct_it = ct_map.find(ec.type_id);
    if (ct_it == ct_map.end())
    {
        errors.push_back("existing container type_id '" + ec.type_id + "' not found in container_types");
        return load;
    }
    load.type_id = ec.type_id;
    load.type = ct_it->second;
    load.locked = true;

    for (const auto& ep : ec.placements)
    {
        auto bt_it = bt_map.find(ep.box_type_id);
        if (bt_it == bt_map.end())
        {
            errors.push_back("existing placement box_type_id '" + ep.box_type_id + "' not found");
            continue;
        }
        Placement pl;
        pl.box_id = ep.box_id;
        pl.box_type_id = ep.box_type_id;
        pl.position = ep.position;
        pl.orientation = ep.orientation;
        pl.osize = bt_it->second.size.orient(ep.orientation);
        pl.platform = ep.platform;
        pl.group = ep.group;
        pl.weight = ep.weight;

        // size 字段如果提供，必须与 type+朝向推导一致
        if (ep.size.has_value())
        {
            const auto& sz = ep.size.value();
            if (sz.dx != pl.osize.dx || sz.dy != pl.osize.dy || sz.dz != pl.osize.dz)
            {
                errors.push_back("existing placement size mismatch for box '" + ep.box_id +
                                 "': expected " + std::to_string(pl.osize.dx) + "x" +
                                 std::to_string(pl.osize.dy) + "x" + std::to_string(pl.osize.dz) +
                                 ", got " + std::to_string(sz.dx) + "x" +
                                 std::to_string(sz.dy) + "x" + std::to_string(sz.dz));
                continue;
            }
        }

        load.placements.push_back(pl);
        load.used_volume += pl.osize.volume();
        if (!ep.platform.empty())
        {
            load.platforms.insert(ep.platform);
        }
        if (!ep.group.empty())
        {
            load.groups.insert(ep.group);
        }
        if (ep.weight.has_value())
        {
            load.total_weight += ep.weight.value();
        }
    }

    // 重建堆叠状态（z 排序），使后续装箱/校验可基于正确状态
    recompute_stack_state(load, bt_map, nullptr);

    return load;
}

void to_json(json& j, const Solution& sol)
{
    j["status"] = status_to_string(sol.status);

    json summary;
    summary["elapsed_second"] = sol.elapsed_second;
    summary["packed_box_count"] = sol.packed_box_count;
    summary["unpacked_box_count"] = sol.unpacked_box_count;
    summary["container_count"] = sol.objective.container_count;
    summary["platform_split"] = sol.objective.platform_split;
    summary["volume_rate"] = sol.objective.avg_volume_rate;
    summary["group_split"] = sol.objective.group_split_sum;

    j["summary"] = std::move(summary);

    json result;
    json containers_json = json::array();
    for (size_t i = 0; i < sol.container_summaries.size(); ++i)
    {
        const auto& cs = sol.container_summaries[i];
        json cj;

        cj["type_id"] = cs.type_id;
        cj["inner_size"] = json::object();
        cj["inner_size"]["x"] = cs.inner_size.x;
        cj["inner_size"]["y"] = cs.inner_size.y;
        cj["inner_size"]["z"] = cs.inner_size.z;
        cj["max_weight"] = opt_json(cs.max_weight);
        cj["used_volume"] = cs.used_volume;
        cj["used_weight"] = opt_json(cs.used_weight);
        cj["volume_rate"] = cs.volume_rate;
        cj["weight_rate"] = opt_json(cs.weight_rate);
        cj["packed_count"] = cs.packed_count;
        cj["platforms"] = cs.platforms;
        cj["groups"] = cs.groups;

        json placements_json = json::array();
        if (i < sol.container_placements.size())
        {
            for (const auto& pl : sol.container_placements[i])
            {
                json pj;
                pj["box_id"] = pl.box_id;
                pj["box_type_id"] = pl.box_type_id;
                pj["position"] = json::object();
                pj["position"]["x"] = pl.position.x;
                pj["position"]["y"] = pl.position.y;
                pj["position"]["z"] = pl.position.z;
                pj["orientation"] = orientation_to_string(pl.orientation);
                pj["size"] = json::object();
                pj["size"]["dx"] = pl.osize.dx;
                pj["size"]["dy"] = pl.osize.dy;
                pj["size"]["dz"] = pl.osize.dz;
                pj["platform"] = pl.platform.empty() ? json(nullptr) : json(pl.platform);
                pj["group"] = pl.group.empty() ? json(nullptr) : json(pl.group);
                pj["weight"] = opt_json(pl.weight);
                placements_json.push_back(std::move(pj));
            }
        }
        cj["placements"] = std::move(placements_json);

        containers_json.push_back(std::move(cj));
    }
    result["containers"] = std::move(containers_json);

    json box_types_json = json::array();
    for (const auto& bt : sol.box_types)
    {
        json bj;
        bj["id"] = bt.id;
        bj["size"] = json::object();
        bj["size"]["x"] = bt.size.x;
        bj["size"]["y"] = bt.size.y;
        bj["size"]["z"] = bt.size.z;
        json orients = json::array();
        for (auto o : bt.allowed_orientations)
        {
            orients.push_back(orientation_to_string(o));
        }
        bj["allowed_orientations"] = std::move(orients);
        if (!bt.max_stack.empty())
        {
            json ms = json::array();
            for (const auto& v : bt.max_stack)
            {
                ms.push_back(v.has_value() ? json(v.value()) : json(nullptr));
            }
            bj["max_stack"] = std::move(ms);
        }
        if (!bt.max_load.empty())
        {
            json ml = json::array();
            for (const auto& v : bt.max_load)
            {
                ml.push_back(v.has_value() ? json(v.value()) : json(nullptr));
            }
            bj["max_load"] = std::move(ml);
        }
        box_types_json.push_back(std::move(bj));
    }
    result["box_types"] = std::move(box_types_json);

    result["unpacked_boxes"] = sol.unpacked_boxes;

    j["result"] = std::move(result);

    if (!sol.violations.empty())
    {
        j["violations"] = sol.violations;
    }
}

} // namespace pack3d
