#include "io.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json-schema.hpp>
#include <spdlog/spdlog.h>

#include "algorithm/config.hpp"
#include "solver.hpp"

namespace hypercube
{

std::string orientation_to_string(Orientation o) noexcept
{
    switch (o)
    {
        case Orientation::XYZ:
            return "xyz";
        case Orientation::XZY:
            return "xzy";
        case Orientation::YXZ:
            return "yxz";
        case Orientation::YZX:
            return "yzx";
        case Orientation::ZXY:
            return "zxy";
        case Orientation::ZYX:
            return "zyx";
        default:
            assert(false && "Unhandled Orientation enum value");
            return "unknown";
    }
}

Orientation orientation_from_string(const std::string& s) noexcept
{
    if (s == "xyz")
    {
        return Orientation::XYZ;
    }
    if (s == "xzy")
    {
        return Orientation::XZY;
    }
    if (s == "yxz")
    {
        return Orientation::YXZ;
    }
    if (s == "yzx")
    {
        return Orientation::YZX;
    }
    if (s == "zxy")
    {
        return Orientation::ZXY;
    }
    if (s == "zyx")
    {
        return Orientation::ZYX;
    }
    assert(false && "Unhandled Orientation string value");
    return Orientation::XYZ;
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
    bt.stackable = j.value("stackable", true);
}

void from_json(const json& j, Box& bx)
{
    j["id"].get_to(bx.id);
    j["box_type_id"].get_to(bx.box_type_id);
    bx.weight = json_opt_double(j, "weight");
    bx.group = j.value("group", std::string());
    bx.platform = j.value("platform", std::string());
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

    if (j.contains("objectives"))
    {
        for (const auto& obj : j["objectives"])
        {
            p.objective_keys.push_back(obj.get<std::string>());
        }
    }

    if (j.contains("algorithm"))
    {
        p.algorithm = algorithm_from_string(j["algorithm"].get<std::string>());
    }
}

std::string algorithm_to_string(Algorithm a) noexcept
{
    switch (a)
    {
        case Algorithm::SGEP:
            return "sgep";
        case Algorithm::MLHS:
            return "mlhs";
        case Algorithm::RGS:
            return "rgs";
        default:
            assert(false && "Unhandled Algorithm enum value");
            return "unknown";
    }
}

Algorithm algorithm_from_string(const std::string& s) noexcept
{
    if (s == "mlhs")
    {
        return Algorithm::MLHS;
    }
    if (s == "rgs")
    {
        return Algorithm::RGS;
    }
    return Algorithm::SGEP;
}

std::string status_to_string(SolveStatus s) noexcept
{
    switch (s)
    {
        case SolveStatus::Complete:
            return "complete";
        case SolveStatus::Invalid:
            return "invalid";
        case SolveStatus::Blocked:
            return "blocked";
        case SolveStatus::Timeout:
            return "timeout";
        case SolveStatus::Partial:
            return "partial";
        default:
            assert(false && "Unhandled SolveStatus enum value");
            return "unknown";
    }
}

// 从 data/input_schema.json 读取 schema 并校验 JSON
std::vector<std::string> validate_schema(const json& j) noexcept
{
    std::vector<std::string> out;
    try
    {
        std::ifstream ifs("data/input_schema.json");
        if (!ifs.is_open())
        {
            out.push_back("cannot open data/input_schema.json");
            return out;
        }
        std::stringstream buf;
        buf << ifs.rdbuf();
        auto schema = json::parse(buf.str());
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
    }

    return out;
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

    json keys = json::array();
    for (const auto& key : sol.objective_keys)
    {
        keys.push_back(key);
    }
    summary["objective_keys"] = std::move(keys);

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
                pj["platform"] = pl.platform.empty() ? json(nullptr) : json(pl.platform);
                pj["group"] = pl.group.empty() ? json(nullptr) : json(pl.group);
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

} // namespace hypercube
