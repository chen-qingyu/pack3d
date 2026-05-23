#include "io.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <variant>

#include <nlohmann/json-schema.hpp>
#include <spdlog/spdlog.h>

#include "solver.hpp"

namespace hypercube
{

// Status 字符串互转
std::string status_to_string(Status s) noexcept
{
    switch (s)
    {
        case Status::Success:
            return "success";
        case Status::Timeout:
            return "timeout";
        case Status::FailedConstraint:
            return "failed_constraint";
        case Status::InvalidInput:
            return "invalid_input";
        default:
            assert(false && "Unhandled Status enum value");
            return "unknown";
    }
}

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

std::optional<Problem> problem_from_json(const json& j) noexcept
{
    try
    {
        Problem p;

        // container_types
        for (const auto& item : j["container_types"])
        {
            ContainerType ct;
            ct.id = item["id"].get<std::string>();
            ct.inner_size.x = item["inner_size"]["x"].get<int32_t>();
            ct.inner_size.y = item["inner_size"]["y"].get<int32_t>();
            ct.inner_size.z = item["inner_size"]["z"].get<int32_t>();
            ct.max_weight = item["max_weight"].get<double>();
            ct.quantity_limit = json_opt_int(item, "quantity_limit");
            p.container_types.push_back(std::move(ct));
        }

        // box_types
        for (const auto& item : j["box_types"])
        {
            BoxType bt;
            bt.id = item["id"].get<std::string>();
            bt.size.x = item["size"]["x"].get<int32_t>();
            bt.size.y = item["size"]["y"].get<int32_t>();
            bt.size.z = item["size"]["z"].get<int32_t>();
            for (const auto& o_str : item["allowed_orientations"])
            {
                bt.allowed_orientations.push_back(
                    orientation_from_string(o_str.get<std::string>()));
            }
            p.box_types.push_back(std::move(bt));
        }

        // boxes
        for (const auto& item : j["boxes"])
        {
            Box bx;
            bx.id = item["id"].get<std::string>();
            bx.box_type_id = item["box_type_id"].get<std::string>();
            bx.weight = item["weight"].get<double>();
            bx.group = item.value("group", std::string());
            bx.platform = item.value("platform", std::string());
            p.boxes.push_back(std::move(bx));
        }

        // constraints
        if (j.contains("constraints"))
        {
            const auto& c = j["constraints"];
            p.time_limit_seconds = c.value("time_limit_seconds", 120.0);
            p.support_rate = c.value("support_rate", 0.0);
            p.platform_limit = json_opt_int(c, "platform_limit");
            p.tender_limit = json_opt_int(c, "tender_limit");

            if (c.contains("route"))
            {
                RouteOrder route;
                for (const auto& plat : c["route"])
                {
                    std::string pname = plat.get<std::string>();
                    route.index_of[pname] = route.platform_order.size();
                    route.platform_order.push_back(std::move(pname));
                }
                p.route = std::move(route);
            }
        }

        // objectives
        if (j.contains("objectives"))
        {
            for (const auto& obj : j["objectives"])
            {
                p.objective_keys.push_back(obj.get<std::string>());
            }
        }

        // solver
        if (j.contains("solver"))
        {
            const auto& s = j["solver"];
            p.solver_config.strategy = s.value("strategy", "constructive_multi_active");
            p.solver_config.active_container_limit = s.value("active_container_limit", 3);
            p.solver_config.random_seed = s.value("random_seed", 42);
        }

        return p;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

// 从 data/input_schema.json 读取 schema 并校验 JSON
std::vector<Violation> validate_schema(const json& j) noexcept
{
    std::vector<Violation> out;
    try
    {
        std::ifstream ifs("data/input_schema.json");
        if (!ifs.is_open())
        {
            out.push_back({"schema_internal", {}, "cannot open data/input_schema.json"});
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
        out.push_back({"schema_validation", {}, e.what()});
    }
    return out;
}

// 重复 ID 检查
template <typename T>
std::vector<Violation> check_duplicate_ids(const std::vector<T>& items,
                                           const char* kind)
{
    std::vector<Violation> out;
    std::set<std::string> seen;
    for (const auto& item : items)
    {
        if (!seen.insert(item.id).second)
        {
            out.push_back({std::string(kind),
                           {item.id},
                           reason::k_duplicate_id});
        }
    }
    return out;
}

// 预校验输入（schema 无法表达的跨字段校验），空列表表示通过
std::vector<Violation> pre_validate_input(const Problem& problem) noexcept
{
    std::vector<Violation> out;

    auto dup_ct = check_duplicate_ids(problem.container_types, "container_type");
    out.insert(out.end(), dup_ct.begin(), dup_ct.end());

    auto dup_bt = check_duplicate_ids(problem.box_types, "box_type");
    out.insert(out.end(), dup_bt.begin(), dup_bt.end());

    auto dup_bx = check_duplicate_ids(problem.boxes, "box");
    out.insert(out.end(), dup_bx.begin(), dup_bx.end());

    std::set<std::string> bt_ids;
    for (const auto& bt : problem.box_types)
    {
        bt_ids.insert(bt.id);
    }
    for (const auto& bx : problem.boxes)
    {
        if (!bt_ids.count(bx.box_type_id))
        {
            out.push_back({"unknown_box_type", {bx.id, bx.box_type_id}, "unknown_box_type"});
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
                out.push_back({"route_duplicate", {p}, reason::k_route_missing_platform});
            }
        }

        for (const auto& bx : problem.boxes)
        {
            if (!bx.platform.empty() && !route.index_of.count(bx.platform))
            {
                out.push_back({"route_platform_missing",
                               {bx.id, bx.platform},
                               reason::k_route_missing_platform});
            }
        }
    }

    return out;
}

std::variant<Problem, Solution> parse_json(const std::string& json_text) noexcept
{
    try
    {
        auto j = json::parse(json_text);

        // schema 校验
        auto schema_errors = validate_schema(j);
        if (!schema_errors.empty())
        {
            Solution s;
            s.status = Status::InvalidInput;
            s.reason = reason::k_invalid_range;
            s.violations = std::move(schema_errors);
            return s;
        }

        auto problem = problem_from_json(j);
        if (!problem.has_value())
        {
            Solution s;
            s.status = Status::InvalidInput;
            s.reason = reason::k_invalid_range;
            s.violations.push_back({"json_parse", {}, "failed_to_parse_json_structure"});
            return s;
        }

        // 运行预校验
        auto violations = pre_validate_input(problem.value());
        if (!violations.empty())
        {
            Solution s;
            s.status = Status::InvalidInput;
            // 用首个违规的 details 作为 reason（仅含 schema 无法表达的跨字段校验）
            s.reason = violations.empty() ? reason::k_invalid_range : violations[0].details;
            s.violations = std::move(violations);
            return s;
        }

        return problem.value();
    }
    catch (const json::parse_error& e)
    {
        Solution s;
        s.status = Status::InvalidInput;
        s.reason = reason::k_invalid_range;
        s.violations.push_back({"json_syntax", {}, e.what()});
        return s;
    }
    catch (const std::exception& e)
    {
        Solution s;
        s.status = Status::InvalidInput;
        s.reason = reason::k_invalid_range;
        s.violations.push_back({"json_parse", {}, e.what()});
        return s;
    }
}

json solution_to_json(const Solution& sol) noexcept
{
    json j;

    j["status"] = status_to_string(sol.status);
    j["reason"] = sol.reason;

    json summary;
    summary["elapsed_second"] = sol.elapsed_second;
    summary["packed_box_count"] = sol.packed_box_count;
    summary["unpacked_box_count"] = sol.unpacked_box_count;
    summary["container_count"] = sol.objective.container_count;
    summary["platform_count"] = sol.objective.platform_count;
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

        cj["id"] = cs.id;
        cj["type_id"] = cs.type_id;
        cj["inner_size"] = json::object();
        cj["inner_size"]["x"] = cs.inner_size.x;
        cj["inner_size"]["y"] = cs.inner_size.y;
        cj["inner_size"]["z"] = cs.inner_size.z;

        json ls;
        ls["used_volume"] = cs.used_volume;
        ls["total_volume"] = cs.total_volume;
        ls["volume_rate"] = cs.volume_rate;
        ls["total_weight"] = cs.total_weight;
        ls["platforms"] = cs.platforms;
        ls["groups"] = cs.groups;
        cj["load_summary"] = std::move(ls);

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
        json violations_json = json::array();
        for (const auto& v : sol.violations)
        {
            json vj;
            vj["kind"] = v.kind;
            vj["subject_ids"] = v.subject_ids;
            vj["details"] = v.details;
            violations_json.push_back(std::move(vj));
        }
        j["violations"] = std::move(violations_json);
    }

    return j;
}

} // namespace hypercube
