#include "io.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json-schema.hpp>
#include <spdlog/spdlog.h>

#include "constraints.hpp"
#include "input_schema.h"
#include "pallet.hpp"

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

template <typename T>
json opt_json(const std::optional<T>& opt) noexcept
{
    return opt.has_value() ? json(opt.value()) : json(nullptr);
}

// 从 JSON 中获取可选的标量，key 缺失或 null 视为空 optional
template <typename T>
std::optional<T> json_opt(const json& j, const char* key)
{
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
    {
        return std::nullopt;
    }
    return it->get<T>();
}

enum class FieldSource : uint8_t
{
    Invalid,
    None,
    Type,
    Instance,
};

struct FieldPresence
{
    bool any = false;
    bool all = true;

    void observe(bool configured) noexcept
    {
        any |= configured;
        all &= configured;
    }
};

struct LabelSources
{
    FieldSource group = FieldSource::Invalid;
    FieldSource platform = FieldSource::Invalid;
};

template <typename TypeGetter, typename BoxGetter>
FieldSource detect_three_way_source(const char* name,
                                    const std::vector<BoxType>& box_types,
                                    const std::vector<Box>& boxes,
                                    TypeGetter type_value,
                                    BoxGetter box_value,
                                    std::vector<std::string>& errors)
{
    FieldPresence type;
    FieldPresence instance;
    for (const auto& bt : box_types)
    {
        type.observe(type_value(bt).has_value());
    }
    for (const auto& bx : boxes)
    {
        instance.observe(box_value(bx).has_value());
    }
    if ((type.any && instance.any) || (type.any && !type.all) ||
        (instance.any && !instance.all))
    {
        errors.push_back(std::string("inconsistent ") + name +
                         ": use none, all box_types, or all instances");
        return FieldSource::Invalid;
    }
    if (type.any)
    {
        return FieldSource::Type;
    }
    if (instance.any)
    {
        return FieldSource::Instance;
    }
    return FieldSource::None;
}

std::optional<std::string> optional_string(const std::string& value)
{
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

LabelSources detect_label_sources(const Problem& problem,
                                  std::vector<std::string>& errors)
{
    for (const auto& bt : problem.box_types)
    {
        if (bt.group.has_value())
        {
            if (bt.group->empty())
            {
                errors.push_back("box_type " + bt.id + ": group must be non-empty when configured");
            }
        }
        if (bt.platform.has_value())
        {
            if (bt.platform->empty())
            {
                errors.push_back("box_type " + bt.id + ": platform must be non-empty when configured");
            }
        }
    }

    return {
        detect_three_way_source("group", problem.box_types, problem.boxes, [](const BoxType& bt)
                                { return bt.group; }, [](const Box& bx)
                                { return optional_string(bx.group); }, errors),
        detect_three_way_source("platform", problem.box_types, problem.boxes, [](const BoxType& bt)
                                { return bt.platform; }, [](const Box& bx)
                                { return optional_string(bx.platform); }, errors),
    };
}

template <typename T, typename TypeGetter, typename SnapshotGetter, typename Equal>
void check_snapshot_consistency(
    const char* name,
    FieldSource source,
    const std::map<std::string, const BoxType*>& type_map,
    const std::vector<ExistingContainer>& existing,
    TypeGetter type_value,
    SnapshotGetter snapshot_value,
    Equal equal,
    std::vector<std::string>& errors)
{
    for (const auto& ec : existing)
    {
        for (const auto& ep : ec.placements)
        {
            auto it = type_map.find(ep.box_type_id);
            if (it == type_map.end())
            {
                continue;
            }
            const auto type = type_value(*it->second);
            const auto snapshot = snapshot_value(ep);
            const bool valid =
                source == FieldSource::None ? !snapshot.has_value() : source == FieldSource::Type ? !snapshot.has_value() || (type.has_value() && equal(*type, *snapshot))
                                                                                                  : snapshot.has_value();
            if (!valid)
            {
                errors.push_back(std::string("inconsistent ") + name +
                                 ": existing placement violates source mode for box_type " +
                                 ep.box_type_id);
            }
        }
    }
}

template <typename T, typename Objects, typename TypeGetter,
          typename ValueGetter, typename Setter>
void inherit_type_field(FieldSource source,
                        const std::map<std::string, const BoxType*>& type_map,
                        Objects& objects,
                        TypeGetter type_value,
                        ValueGetter object_value,
                        Setter set_value)
{
    if (source != FieldSource::Type)
    {
        return;
    }
    for (auto& object : objects)
    {
        auto it = type_map.find(object.box_type_id);
        if (it == type_map.end())
        {
            continue;
        }
        const auto type = type_value(*it->second);
        if (type.has_value() && !object_value(object).has_value())
        {
            set_value(object, *type);
        }
    }
}

std::string field_value(const BoxType* bt, const std::string& instance,
                        FieldSource source, bool group)
{
    if (source == FieldSource::Type && bt != nullptr)
    {
        return (group ? bt->group : bt->platform).value_or(std::string());
    }
    return source == FieldSource::Instance ? instance : std::string();
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

void from_json(const json& j, Obstacle& o)
{
    j["x"].get_to(o.x);
    j["y"].get_to(o.y);
    j["z"].get_to(o.z);
    j["dx"].get_to(o.dx);
    j["dy"].get_to(o.dy);
    j["dz"].get_to(o.dz);
}

void from_json(const json& j, Facet& f)
{
    f.dx = json_opt<int>(j, "dx").value_or(0);
    f.dy = json_opt<int>(j, "dy").value_or(0);
    f.dz = json_opt<int>(j, "dz").value_or(0);
}

void from_json(const json& j, ContainerType& ct)
{
    j["id"].get_to(ct.id);
    j["sx"].get_to(ct.inner_size.x);
    j["sy"].get_to(ct.inner_size.y);
    j["sz"].get_to(ct.inner_size.z);
    ct.payload = json_opt<double>(j, "payload");
    ct.quantity_limit = json_opt<int>(j, "quantity_limit");
    if (j.contains("obstacles"))
    {
        j["obstacles"].get_to(ct.obstacles);
    }
    if (j.contains("facets"))
    {
        j["facets"].get_to(ct.facets);
    }
}

void from_json(const json& j, BoxType& bt)
{
    j["id"].get_to(bt.id);
    j["sx"].get_to(bt.size.x);
    j["sy"].get_to(bt.size.y);
    j["sz"].get_to(bt.size.z);
    for (const auto& o_str : j["allowed_orientations"])
    {
        bt.allowed_orientations.push_back(orientation_from_string(o_str.get<std::string>()));
    }
    parse_optional_vector<int>(j, "max_stack", bt.max_stack, bt.allowed_orientations.size());
    parse_optional_vector<double>(j, "max_load", bt.max_load, bt.allowed_orientations.size());
    bt.weight = json_opt<double>(j, "weight");
    bt.loose = j.value("loose", false);
    bt.group = json_opt<std::string>(j, "group");
    bt.platform = json_opt<std::string>(j, "platform");
}

void from_json(const json& j, Box& bx)
{
    j["id"].get_to(bx.id);
    j["box_type_id"].get_to(bx.box_type_id);
    bx.weight = json_opt<double>(j, "weight");
    bx.group = json_opt<std::string>(j, "group").value_or(std::string());
    bx.platform = json_opt<std::string>(j, "platform").value_or(std::string());
}

void from_json(const json& j, ExistingPlacement& ep)
{
    j["box_id"].get_to(ep.box_id);
    j["box_type_id"].get_to(ep.box_type_id);
    j["x"].get_to(ep.position.x);
    j["y"].get_to(ep.position.y);
    j["z"].get_to(ep.position.z);
    ep.orientation = orientation_from_string(j["orientation"].get<std::string>());
    ep.weight = json_opt<double>(j, "weight");
    ep.platform = json_opt<std::string>(j, "platform").value_or(std::string());
    ep.group = json_opt<std::string>(j, "group").value_or(std::string());
    if (j.contains("dx"))
    {
        OrientedSize sz;
        j["dx"].get_to(sz.dx);
        j["dy"].get_to(sz.dy);
        j["dz"].get_to(sz.dz);
        ep.size = sz;
    }
}

void from_json(const json& j, ExistingContainer& ec)
{
    j["type_id"].get_to(ec.type_id);
    j["placements"].get_to(ec.placements);
}

void from_json(const json& j, Problem& p)
{
    // schema 已保证必需字段存在；缺省值由 Problem 成员初始化提供（唯一来源）
    j["container_types"].get_to(p.container_types);
    j["box_types"].get_to(p.box_types);
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
    j["boxes"].get_to(p.boxes);

    if (j.contains("pallet_types"))
    {
        j["pallet_types"].get_to(p.pallet_types);
    }

    // 仅当 JSON 显式给出时覆盖默认值
    if (j.contains("constraints"))
    {
        const auto& c = j["constraints"];
        if (auto v = json_opt<double>(c, "time_limit"))
        {
            p.time_limit = *v;
        }
        if (auto v = json_opt<double>(c, "support_rate"))
        {
            p.support_rate = *v;
        }
        p.platform_limit = json_opt<int>(c, "platform_limit");
        p.tender_limit = json_opt<int>(c, "tender_limit");
        if (auto v = json_opt<bool>(c, "pallet_fallback"))
        {
            p.pallet_fallback = *v;
        }
        if (auto v = json_opt<double>(c, "pallet_support_rate"))
        {
            p.pallet_support_rate = *v;
        }
        if (auto v = json_opt<bool>(c, "heavy_not_on_light"))
        {
            p.heavy_not_on_light = *v;
        }
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
        j["existing_containers"].get_to(p.existing_containers);
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

    // 重复 ID 校验（容器类型 / 箱型 / 箱子）
    auto check_dup = [&](const auto& items, const char* what)
    {
        std::set<std::string> seen;
        for (const auto& it : items)
        {
            if (!seen.insert(it.id).second)
            {
                out.push_back(std::string("duplicate ") + what + " id: " + it.id);
            }
        }
    };
    check_dup(problem.container_types, "container_type");

    // 障碍物校验：完全在容器内、互不重叠
    for (const auto& ct : problem.container_types)
    {
        for (size_t oi = 0; oi < ct.obstacles.size(); ++oi)
        {
            const auto& o = ct.obstacles[oi];
            std::string opfx = "container_type " + ct.id + " obstacles[" + std::to_string(oi) + "]";
            if (o.x < 0 || o.y < 0 || o.z < 0 ||
                o.x + o.dx > ct.inner_size.x ||
                o.y + o.dy > ct.inner_size.y ||
                o.z + o.dz > ct.inner_size.z)
            {
                out.push_back(opfx + ": out of container bounds");
            }
            for (size_t oj = 0; oj < oi; ++oj)
            {
                const auto& q = ct.obstacles[oj];
                if (o.x < q.x + q.dx && o.x + o.dx > q.x &&
                    o.y < q.y + q.dy && o.y + o.dy > q.y &&
                    o.z < q.z + q.dz && o.z + o.dz > q.z)
                {
                    out.push_back(opfx + ": overlaps with obstacles[" + std::to_string(oj) + "]");
                }
            }
        }
    }
    // 斜面校验：恰好两个非零截距、截距不越界
    for (const auto& ct : problem.container_types)
    {
        for (size_t fi = 0; fi < ct.facets.size(); ++fi)
        {
            const auto& f = ct.facets[fi];
            std::string fpfx = "container_type " + ct.id + " facets[" + std::to_string(fi) + "]";
            int32_t intercepts[3] = {f.dx, f.dy, f.dz};
            int32_t extents[3] = {ct.inner_size.x, ct.inner_size.y, ct.inner_size.z};
            int count = 0;
            for (int a = 0; a < 3; ++a)
            {
                if (intercepts[a] != 0)
                {
                    ++count;
                }
            }
            if (count != 2)
            {
                out.push_back(fpfx + ": must have exactly two non-zero intercepts (dx/dy/dz)");
            }
            for (int a = 0; a < 3; ++a)
            {
                if (intercepts[a] == 0)
                {
                    continue;
                }
                int64_t mag = std::abs(static_cast<int64_t>(intercepts[a]));
                if (mag < 1 || mag > extents[a])
                {
                    out.push_back(fpfx + ": intercept out of container bounds");
                }
            }
        }
    }
    check_dup(problem.box_types, "box_type");
    check_dup(problem.boxes, "box");

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

    const auto label_sources = detect_label_sources(problem, out);
    std::map<std::string, const BoxType*> bt_map;
    for (const auto& bt : problem.box_types)
    {
        bt_map[bt.id] = &bt;
    }
    auto input_label = [&](const std::string& box_type_id,
                           const std::string& instance_value,
                           FieldSource source, bool group)
    {
        auto it = bt_map.find(box_type_id);
        const BoxType* bt = it == bt_map.end() ? nullptr : it->second;
        return field_value(bt, instance_value, source, group);
    };

    check_snapshot_consistency<std::string>(
        "group", label_sources.group, bt_map, problem.existing_containers,
        [](const BoxType& bt)
        { return bt.group; },
        [](const ExistingPlacement& ep)
        { return optional_string(ep.group); },
        std::equal_to<std::string>{}, out);
    check_snapshot_consistency<std::string>(
        "platform", label_sources.platform, bt_map, problem.existing_containers,
        [](const BoxType& bt)
        { return bt.platform; },
        [](const ExistingPlacement& ep)
        { return optional_string(ep.platform); },
        std::equal_to<std::string>{}, out);

    // 重量信息一致性：与 group/platform 共用严格三选一检测
    const auto weight_source = detect_three_way_source(
        "weight", problem.box_types, problem.boxes,
        [](const BoxType& bt)
        { return bt.weight; },
        [](const Box& bx)
        { return bx.weight; }, out);
    check_snapshot_consistency<double>(
        "weight", weight_source, bt_map, problem.existing_containers,
        [](const BoxType& bt)
        { return bt.weight; },
        [](const ExistingPlacement& ep)
        { return ep.weight; },
        [](double a, double b)
        { return std::abs(a - b) <= 1e-9; }, out);
    const bool weight_info = weight_source != FieldSource::None;
    if (weight_info)
    {
        for (const auto& ct : problem.container_types)
        {
            if (!ct.payload.has_value())
            {
                out.push_back("inconsistent weight: container_type " + ct.id + " missing payload");
            }
        }
    }

    // group 前提按归一化后的有效值判断（输出 tender 要么全数字要么全 null）
    bool any_group = false;
    for (const auto& bx : problem.boxes)
    {
        any_group |= !input_label(bx.box_type_id, bx.group, label_sources.group, true).empty();
    }
    for (const auto& ec : problem.existing_containers)
    {
        for (const auto& ep : ec.placements)
        {
            any_group |= !input_label(ep.box_type_id, ep.group, label_sources.group, true).empty();
        }
    }

    // tender_limit 需要 group：无 group 时 check_tender_limit 对空 group 恒真，约束静默失效
    if (problem.tender_limit > 0 && !any_group)
    {
        out.push_back("tender_limit requires group (no box or existing placement has group)");
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
    if (any_max_load && !weight_info)
    {
        out.push_back("inconsistent weight: max_load requires weight info (box_types or boxes)");
    }
    if (problem.heavy_not_on_light && !weight_info)
    {
        out.push_back("inconsistent weight: heavy_not_on_light requires weight info (box_types or boxes)");
    }

    // max_stack/max_load 物理有效的前提：support_rate>0（support_rate=0 允许悬空放置，
    // 悬空箱不计入堆叠柱，可绕过堆码层数与单箱承重约束）
    if ((any_max_stack || any_max_load) && problem.support_rate == 0.0)
    {
        out.push_back("max_stack/max_load requires support_rate > 0");
    }
    // 重不压轻同样以支撑率为物理前提（悬空箱无直接支撑，可绕过比较）
    if (problem.heavy_not_on_light && problem.support_rate == 0.0)
    {
        out.push_back("heavy_not_on_light requires support_rate > 0");
    }

    // 箱子/已有放置的有效 platform 就必须有 route 且平台在路线中
    bool any_platform = false;
    for (const auto& bx : problem.boxes)
    {
        any_platform |= !input_label(bx.box_type_id, bx.platform,
                                     label_sources.platform, false)
                             .empty();
    }
    for (const auto& ec : problem.existing_containers)
    {
        for (const auto& ep : ec.placements)
        {
            any_platform |= !input_label(ep.box_type_id, ep.platform,
                                         label_sources.platform, false)
                                 .empty();
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
            const auto platform = input_label(bx.box_type_id, bx.platform,
                                              label_sources.platform, false);
            if (!platform.empty() && !route.index_of.count(platform))
            {
                out.push_back("platform '" + platform + "' not in route, box " + bx.id);
            }
        }

        for (size_t ei = 0; ei < problem.existing_containers.size(); ++ei)
        {
            const auto& ec = problem.existing_containers[ei];
            for (size_t pi = 0; pi < ec.placements.size(); ++pi)
            {
                const auto& ep = ec.placements[pi];
                const auto platform = input_label(ep.box_type_id, ep.platform,
                                                  label_sources.platform, false);
                if (!platform.empty() && !route.index_of.count(platform))
                {
                    out.push_back("platform '" + platform + "' not in route, existing_containers[" +
                                  std::to_string(ei) + "].placements[" + std::to_string(pi) +
                                  "] (box " + ep.box_id + ")");
                }
            }
        }
    }

    // 装托（palletizing）预校验
    if (!problem.pallet_types.empty())
    {
        std::set<std::string> pt_seen;
        for (const auto& pt : problem.pallet_types)
        {
            if (!pt_seen.insert(pt.id).second)
            {
                out.push_back("duplicate pallet_type id: " + pt.id);
            }
        }

        // 装托模式强制有重量信息（箱型级或箱子级）；容器必须有 payload（max_height 下限由 schema 保证）
        if (!weight_info)
        {
            out.push_back("inconsistent weight: pallet mode requires weight info (box_types or boxes)");
        }
        bool all_ct_have_weight = true;
        for (const auto& ct : problem.container_types)
        {
            if (!ct.payload.has_value())
            {
                all_ct_have_weight = false;
                break;
            }
        }
        if (!all_ct_have_weight)
        {
            out.push_back("inconsistent weight: pallet mode requires all container_types to have payload");
        }

        // 装托模式要求装车支撑率 > 0：support_rate=0 时托盘可在车厢内悬空，物理不合理
        if (problem.support_rate == 0.0)
        {
            out.push_back("pallet mode requires support_rate > 0");
        }
    }

    // 任一箱型声明 loose:true 但未提供 pallet_types → 配置错误
    bool any_loose = false;
    for (const auto& bt : problem.box_types)
    {
        any_loose |= bt.loose;
    }
    if (any_loose && problem.pallet_types.empty())
    {
        out.push_back("loose declared but no pallet_types");
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

                if (check_obstacle(pl.position, pl.osize, ct.obstacles))
                {
                    out.push_back(pfx + " (" + pl.box_id + "): overlaps with container obstacle");
                }

                if (check_facet(pl.position, pl.osize, ct.inner_size, ct.facets))
                {
                    out.push_back(pfx + " (" + pl.box_id + "): violates container facet");
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
            if (ct.payload.has_value() && load.total_weight > ct.payload.value() + 1e-9)
            {
                out.push_back(prefix + ": total weight " + std::to_string(load.total_weight) +
                              " exceeds payload " + std::to_string(ct.payload.value()));
            }
        }
    }

    return out;
}

void resolve_type_fields(Problem& p) noexcept
{
    std::vector<std::string> ignored_errors;
    const auto weight_source = detect_three_way_source(
        "weight", p.box_types, p.boxes,
        [](const BoxType& bt)
        { return bt.weight; },
        [](const Box& bx)
        { return bx.weight; }, ignored_errors);
    const auto label_sources = detect_label_sources(p, ignored_errors);
    std::map<std::string, const BoxType*> type_map;
    for (const auto& bt : p.box_types)
    {
        type_map[bt.id] = &bt;
    }
    inherit_type_field<double>(weight_source, type_map, p.boxes, [](const BoxType& bt)
                               { return bt.weight; }, [](const Box& bx)
                               { return bx.weight; }, [](Box& bx, double value)
                               { bx.weight = value; });
    inherit_type_field<std::string>(label_sources.group, type_map, p.boxes, [](const BoxType& bt)
                                    { return bt.group; }, [](const Box& bx)
                                    { return optional_string(bx.group); }, [](Box& bx, const std::string& value)
                                    { bx.group = value; });
    inherit_type_field<std::string>(label_sources.platform, type_map, p.boxes, [](const BoxType& bt)
                                    { return bt.platform; }, [](const Box& bx)
                                    { return optional_string(bx.platform); }, [](Box& bx, const std::string& value)
                                    { bx.platform = value; });
    for (auto& ec : p.existing_containers)
    {
        inherit_type_field<double>(weight_source, type_map, ec.placements, [](const BoxType& bt)
                                   { return bt.weight; }, [](const ExistingPlacement& ep)
                                   { return ep.weight; }, [](ExistingPlacement& ep, double value)
                                   { ep.weight = value; });
        inherit_type_field<std::string>(label_sources.group, type_map, ec.placements, [](const BoxType& bt)
                                        { return bt.group; }, [](const ExistingPlacement& ep)
                                        { return optional_string(ep.group); }, [](ExistingPlacement& ep, const std::string& value)
                                        { ep.group = value; });
        inherit_type_field<std::string>(label_sources.platform, type_map, ec.placements, [](const BoxType& bt)
                                        { return bt.platform; }, [](const ExistingPlacement& ep)
                                        { return optional_string(ep.platform); }, [](ExistingPlacement& ep, const std::string& value)
                                        { ep.platform = value; });
    }
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
        pl.platform = ep.platform.empty() ? bt_it->second.platform.value_or(std::string()) : ep.platform;
        pl.group = ep.group.empty() ? bt_it->second.group.value_or(std::string()) : ep.group;
        pl.weight = ep.weight;
        // 箱型级重量缺省：已有放置未显式给重量时取箱型重量
        if (!pl.weight.has_value() && bt_it->second.weight.has_value())
        {
            pl.weight = bt_it->second.weight;
        }

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
        if (pl.weight.has_value())
        {
            load.total_weight += pl.weight.value();
        }
    }

    // 重建堆叠状态（z 排序），使后续装箱/校验可基于正确状态
    recompute_stack_state(load, bt_map, nullptr);

    return load;
}

void to_json(json& j, const Placement& pl)
{
    j["box_id"] = pl.box_id;
    j["box_type_id"] = pl.box_type_id;
    j["x"] = pl.position.x;
    j["y"] = pl.position.y;
    j["z"] = pl.position.z;
    j["orientation"] = orientation_to_string(pl.orientation);
    j["dx"] = pl.osize.dx;
    j["dy"] = pl.osize.dy;
    j["dz"] = pl.osize.dz;
    j["platform"] = pl.platform.empty() ? json(nullptr) : json(pl.platform);
    j["group"] = pl.group.empty() ? json(nullptr) : json(pl.group);
    j["weight"] = opt_json(pl.weight);
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
    // X 方向口径平均（口径见 docs/output.md）
    double sum_rate_x = 0.0;
    for (const auto& cs : sol.container_summaries)
    {
        sum_rate_x += cs.volume_rate_x;
    }
    summary["volume_rate_x"] = !sol.container_summaries.empty()
                                   ? sum_rate_x / static_cast<double>(sol.container_summaries.size())
                                   : 0.0;
    summary["group_split"] = sol.objective.group_split_sum;
    // 装托字段恒输出：未启用装托时全为 0 / 空数组
    summary["pallet_count"] = sol.pallet_count;
    summary["palletized_box_count"] = sol.palletized_box_count;
    summary["loose_box_count"] = sol.loose_box_count;

    j["summary"] = std::move(summary);

    json result;
    json containers_json = json::array();
    for (size_t i = 0; i < sol.container_summaries.size(); ++i)
    {
        const auto& cs = sol.container_summaries[i];
        json cj;

        cj["type_id"] = cs.type_id;
        cj["sx"] = cs.inner_size.x;
        cj["sy"] = cs.inner_size.y;
        cj["sz"] = cs.inner_size.z;
        // obstacles/facets 恒输出（未启用为空数组）
        json obs_json = json::array();
        for (const auto& o : cs.obstacles)
        {
            json oj;
            oj["x"] = o.x;
            oj["y"] = o.y;
            oj["z"] = o.z;
            oj["dx"] = o.dx;
            oj["dy"] = o.dy;
            oj["dz"] = o.dz;
            obs_json.push_back(std::move(oj));
        }
        cj["obstacles"] = std::move(obs_json);

        json fac_json = json::array();
        for (const auto& f : cs.facets)
        {
            json fj;
            if (f.dx != 0)
            {
                fj["dx"] = f.dx;
            }
            if (f.dy != 0)
            {
                fj["dy"] = f.dy;
            }
            if (f.dz != 0)
            {
                fj["dz"] = f.dz;
            }
            fac_json.push_back(std::move(fj));
        }
        cj["facets"] = std::move(fac_json);
        cj["payload"] = opt_json(cs.payload);
        cj["used_volume"] = cs.used_volume;
        cj["used_weight"] = opt_json(cs.used_weight);
        cj["volume_rate"] = cs.volume_rate;
        cj["volume_rate_x"] = cs.volume_rate_x;
        cj["weight_rate"] = opt_json(cs.weight_rate);
        cj["packed_count"] = cs.packed_count;
        cj["platforms"] = cs.platforms;
        cj["groups"] = cs.groups;
        cj["tender"] = opt_json(cs.tender);

        json placements_json = json::array();
        if (i < sol.container_placements.size())
        {
            for (const auto& pl : sol.container_placements[i])
            {
                placements_json.push_back(json(pl));
            }
        }
        cj["placements"] = std::move(placements_json);

        containers_json.push_back(std::move(cj));
    }
    result["containers"] = std::move(containers_json);

    // 恒输出（未启用装托为空数组）
    result["pallets"] = sol.pallets;

    json box_types_json = json::array();
    // max_stack/max_load 恒输出（未配置为 null，配置则按朝向数组）
    auto optional_vec = [](const auto& vec) -> json
    {
        if (vec.empty())
        {
            return nullptr;
        }
        json arr = json::array();
        for (const auto& v : vec)
        {
            arr.push_back(v.has_value() ? json(v.value()) : json(nullptr));
        }
        return arr;
    };
    for (const auto& bt : sol.box_types)
    {
        json bj;
        bj["id"] = bt.id;
        bj["sx"] = bt.size.x;
        bj["sy"] = bt.size.y;
        bj["sz"] = bt.size.z;
        json orients = json::array();
        for (auto o : bt.allowed_orientations)
        {
            orients.push_back(orientation_to_string(o));
        }
        bj["allowed_orientations"] = std::move(orients);
        bj["loose"] = bt.loose;
        bj["max_stack"] = optional_vec(bt.max_stack);
        bj["max_load"] = optional_vec(bt.max_load);
        bj["weight"] = opt_json(bt.weight);
        bj["group"] = opt_json(bt.group);
        bj["platform"] = opt_json(bt.platform);
        box_types_json.push_back(std::move(bj));
    }
    result["box_types"] = std::move(box_types_json);

    result["unpacked_boxes"] = sol.unpacked_boxes;

    j["result"] = std::move(result);

    // violations 恒输出（无违规为空数组）
    j["violations"] = sol.violations;
}

} // namespace pack3d
