#include <fstream>
#include <sstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

// 进程启动即全局关闭 spdlog，避免 Catch2 随机测试顺序导致日志时有时无
static const bool g_spdlog_off = (spdlog::set_level(spdlog::level::off), true);

// 从 JSON 文件加载测试场景
static json load_data(const char* path)
{
    std::ifstream ifs(path);
    REQUIRE(ifs.is_open());
    std::stringstream buf;
    buf << ifs.rdbuf();
    return json::parse(buf.str());
}

// 确保 4 个目标固定为默认顺序，3 种算法均可完成
TEST_CASE("fixed_default_objectives", "[solver]")
{
    auto base = load_data("data/demo.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["result"]["containers"].size() >= 1);
        // 输出不再包含 objective_keys 字段
        REQUIRE(!res["summary"].contains("objective_keys"));
    }
}

// run() 对任何非法输入都返回 status=invalid 的 JSON，而不是抛异常/崩溃
TEST_CASE("run 对畸形输入返回 invalid", "[solver]")
{
    // 合法基础结构（从 data/tests/test_invalid_base.json 加载）
    const auto base = load_data("data/tests/test_invalid_base.json");

    std::vector<json> bad_inputs;
    bad_inputs.push_back(json::object()); // 缺必需字段
    bad_inputs.push_back({{"container_types", json::array()},
                          {"box_types", json::array()},
                          {"boxes", json::array()}}); // minItems 违反

    auto dup = base;
    dup["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}},
                    {{"id", "x"}, {"box_type_id", "b"}}}; // 重复 box id
    bad_inputs.push_back(dup);

    auto unknown = base;
    unknown["boxes"] = {{{"id", "x"}, {"box_type_id", "nope"}}}; // 未知箱型
    bad_inputs.push_back(unknown);

    auto no_weight_meta = base;
    no_weight_meta["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}, {"weight", 5.0}}}; // 有重量但容器无 max_weight
    bad_inputs.push_back(no_weight_meta);

    for (const auto& input : bad_inputs)
    {
        const auto res = run(input);
        REQUIRE(res.contains("status"));
        REQUIRE(res["status"] == "invalid");
    }
}

// test_min_container.json — 2 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("min_container_count", "[solver]")
{
    auto base = load_data("data/tests/test_min_container.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }
}

// test_min_platform.json — 大小箱子各 2，A/B 平台
// 固定目标: min_container_count > min_platform_split → 2 大容器，平台不分散
TEST_CASE("min_platform_split", "[solver]")
{
    auto base = load_data("data/tests/test_min_platform.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["platform_split"] == 0);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][0]["platforms"] == json::array({"A"}));
        REQUIRE(res["result"]["containers"][1]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["platforms"] == json::array({"B"}));
    }
}

// test_platform_merge.json — 前车[p1,p2]装满、尾车[p2,p3]未满
// 后处理必须在不新增容器的情况下把分散的 p2 并入尾车，使 platform_split = 0
// 合并 trial 先插入、失败则重排整个目标容器（目标自身箱子 + 捐献箱一起重新装载）。
// RGS 曾因 route 约束下排不出"深处平台先占 X 小侧"的布局而装不下 10 箱，
// 新增 RouteOrder 排序准则（后卸/深处平台优先）后恢复通过。
TEST_CASE("platform_merge_no_new_container", "[solver]")
{
    auto base = load_data("data/tests/test_platform_merge.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);

        // 失败诊断：算法 + 摘要 + 各容器平台分布（INFO 仅在断言失败时输出）
        INFO("algo=" << algo);
        INFO("summary=" << res["summary"].dump());
        std::string plat_desc;
        for (const auto& c : res["result"]["containers"])
        {
            plat_desc += c["type_id"].get<std::string>() + fmt::format(":[{}]", c["platforms"].get<std::vector<std::string>>());
        }
        INFO("containers=" << plat_desc);

        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["platform_split"] == 0);
        for (const auto& c : res["result"]["containers"])
        {
            REQUIRE(c["platforms"].size() <= 2);
        }
    }
}

// test_volume_first.json — 3 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("max_volume_rate", "[solver]")
{
    auto base = load_data("data/tests/test_volume_first.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }
}

// test_group_split.json — 大小箱子各 2，A/B 分组
// 固定目标: min_container_count > min_platform_split > max_volume_rate > min_group_split
TEST_CASE("min_group_split", "[solver]")
{
    auto base = load_data("data/tests/test_group_split.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["group_split"] == 1);
        REQUIRE(res["summary"]["volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
    }
}

TEST_CASE("bsg_enforces_project_hard_constraints", "[solver][bsg]")
{
    json input = load_data("data/tests/test_bsg_constraints.json");

    auto result = run(input);
    REQUIRE(result["status"] == "complete");
    REQUIRE(result["summary"]["unpacked_box_count"] == 0);

    for (const auto& container : result["result"]["containers"])
    {
        CHECK(container["used_weight"].get<double>() <= 10.0);
        CHECK(container["platforms"].size() <= 1);

        const auto& placements = container["placements"];
        for (const auto& lhs : placements)
        {
            for (const auto& rhs : placements)
            {
                if (lhs["platform"] == rhs["platform"])
                {
                    continue;
                }
                bool yz_overlap =
                    lhs["y"].get<int>() < rhs["y"].get<int>() + rhs["dy"].get<int>() &&
                    rhs["y"].get<int>() < lhs["y"].get<int>() + lhs["dy"].get<int>() &&
                    lhs["z"].get<int>() < rhs["z"].get<int>() + rhs["dz"].get<int>() &&
                    rhs["z"].get<int>() < lhs["z"].get<int>() + lhs["dz"].get<int>();
                if (yz_overlap && lhs["platform"] == "A" && rhs["platform"] == "B")
                {
                    // A 先卸 → 应在近门处（X 更大），即 A.x >= B.x + B.dx
                    CHECK(lhs["x"].get<int>() >= rhs["x"].get<int>() + rhs["dx"].get<int>());
                }
            }
        }
    }
}

// 3 个 100 立方体，限堆 2 层 + support_rate=1（禁止悬空）→ 第 3 个无法堆叠 → 需要第 2 个容器
TEST_CASE("max_stack 限制堆码层数", "[solver][stack]")
{
    json input = load_data("data/tests/test_max_stack.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 2);
        for (const auto& c : res["result"]["containers"])
        {
            REQUIRE(c["placements"].size() <= 2);
        }
    }
}

// max_load=40 < 单箱重量 50，support_rate=1（禁止悬空）：
// 每根柱最多 1 箱（任何叠放都会让支撑箱承重 50 > 40）→ 3 个箱子必须分到 3 个容器
TEST_CASE("max_load 限制单箱承重", "[solver][stack]")
{
    json input = load_data("data/tests/test_max_load.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 3);
    }
}

// 非均匀重量下，max_load 用精确重量：容器 used_weight 必须等于各放置重量之和。
// 回归：GLC 之前按平均重量 + 队尾消耗，输出回填按队首，两者对非均匀重量会不一致。
TEST_CASE("max_load 非均匀重量保持重量一致性", "[solver][stack]")
{
    json input = load_data("data/tests/test_max_load_weight.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        for (const auto& c : res["result"]["containers"])
        {
            double sum = 0.0;
            for (const auto& pl : c["placements"])
            {
                sum += pl["weight"].get<double>();
            }
            REQUIRE(c["used_weight"].get<double>() == Catch::Approx(sum));
        }
    }
}

// 按朝向数组：仅 xzy（立放）能装入容器；max_stack=[2,1] 时立放限 1 层 → 需 2 容器
TEST_CASE("max_stack 按朝向数组", "[solver][stack]")
{
    auto make_input = [](int second_orient_limit) -> json
    {
        auto input = load_data("data/tests/test_max_stack_array.json");
        input["box_types"][0]["max_stack"] = {2, second_orient_limit};
        return input;
    };

    // 立放（xzy）限 1 层 → b2 无法叠 → 2 容器
    {
        auto input = make_input(1);
        input["algorithm"] = "gep";
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 2);
    }
    // 立放限 2 层 → 可叠 → 1 容器
    {
        auto input = make_input(2);
        input["algorithm"] = "gep";
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 1);
    }
}

// test_tender_limit.json — 3 箱同 group g1，容器限装 2 箱，tender_limit=1
// g1 只能分散在 1 个容器 → 第 3 箱放不下 → 保持未装（partial）
TEST_CASE("tender_limit 限制每 tender 容器数", "[solver][tender]")
{
    auto input = load_data("data/tests/test_tender_limit.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "partial");
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["unpacked_box_count"] == 1);
        REQUIRE(res["summary"]["packed_box_count"] == 2);
        // 容器带 group → tender 为整数序号；未装箱子不产生容器
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["tender"].get<int>() >= 1);
        // 非 complete 时 violations 说明原因：未装箱的 g1 系 tender_limit 所拒
        REQUIRE(res["violations"].is_array());
        bool has_tender_msg = false;
        for (const auto& v : res["violations"])
        {
            if (v.get<std::string>().find("tender_limit") != std::string::npos)
            {
                has_tender_msg = true;
            }
        }
        REQUIRE(has_tender_msg);
    }
}

// test_tender_output.json — 4 个已有容器：A{g1,g2} B{g2,g3} C{g3,g4} D{g5}
// 连通分量：A-B-C 一个 tender，D 一个 → tender 序号 [1,1,1,2]；无 group 的新容器为 null
TEST_CASE("tender 输出按连通分量编号", "[solver][tender]")
{
    auto input = load_data("data/tests/test_tender_output.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["result"]["containers"].size() == 5);
        REQUIRE(res["result"]["containers"][0]["tender"].get<int>() == 1);
        REQUIRE(res["result"]["containers"][1]["tender"].get<int>() == 1);
        REQUIRE(res["result"]["containers"][2]["tender"].get<int>() == 1);
        REQUIRE(res["result"]["containers"][3]["tender"].get<int>() == 2);
        REQUIRE(res["result"]["containers"][4]["tender"].is_null());
        // 无 group 的容器 platforms/groups 保持 null 语义
        REQUIRE(res["result"]["containers"][0]["groups"].size() == 2);
    }
}

// test_obstacle_step.json — 整宽台阶障碍物 {0,0,0,10,10,5} 占容器 20x10x10 前半
// 可用空间：地板条带 [10,20]x[0,10]x[0,10]（8 箱）+ 台阶顶 [0,10]x[0,10]x[5,10]（4 箱）
// support_rate=1：台阶顶箱子靠"障碍物顶面等价地板"支撑
// 验证：候选点种子/空间雕刻使 4 算法都能装到 12 箱（无种子时 GEP/RGS 会死锁，0 箱）
TEST_CASE("obstacle 台阶可达性与顶面支撑", "[solver][obstacle]")
{
    auto input = load_data("data/tests/test_obstacle_step.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 12);
        REQUIRE(res["summary"]["container_count"] == 1);
        // 输出自包含障碍物几何（逐容器携带）
        REQUIRE(res["result"]["containers"][0]["obstacles"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["obstacles"][0]["dx"] == 10);
        REQUIRE(res["result"]["containers"][0]["obstacles"][0]["dz"] == 5);
    }
}

// 障碍物非法输入：互重叠 / 越界 / 与已有放置冲突 → status=invalid + 具体信息
TEST_CASE("obstacle 非法输入返回 invalid", "[solver][obstacle]")
{
    auto overlap = load_data("data/tests/test_obstacle_overlap.json");
    auto res = run(overlap);
    REQUIRE(res["status"] == "invalid");
    bool has_overlap = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("overlaps with obstacles") != std::string::npos)
        {
            has_overlap = true;
        }
    }
    REQUIRE(has_overlap);

    auto oob = load_data("data/tests/test_obstacle_out_of_bounds.json");
    res = run(oob);
    REQUIRE(res["status"] == "invalid");
    bool has_oob = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("out of container bounds") != std::string::npos)
        {
            has_oob = true;
        }
    }
    REQUIRE(has_oob);

    auto resume = load_data("data/tests/test_obstacle_resume_invalid.json");
    res = run(resume);
    REQUIRE(res["status"] == "invalid");
    bool has_resume = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("overlaps with container obstacle") != std::string::npos)
        {
            has_resume = true;
        }
    }
    REQUIRE(has_resume);
}

// test_facet_chamfer.json — 顶前 45° 斜切 {dx:10,dz:10}（x+z>20 为楔形禁区）
// 物理最大/雕刻后均为 10 箱（后半 8 + 前下 2）；可用容积 = 2000 − 楔形 500 = 1500
// 填充率（可用容积口径）= 1250/1500 ≈ 0.8333
// 验证：禁入（无箱侵入楔形）、填充率达可用容积上限、输出 facets 自包含
TEST_CASE("facet 斜切禁入与填充率", "[solver][facet]")
{
    auto input = load_data("data/tests/test_facet_chamfer.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 10);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["volume_rate"].get<double>() == Catch::Approx(1250.0 / 1500.0));
        // 输出自包含 facets
        REQUIRE(res["result"]["containers"][0]["facets"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["facets"][0]["dx"] == 10);
        REQUIRE(res["result"]["containers"][0]["facets"][0]["dz"] == 10);
        // 无箱子侵入楔形禁区（x+z <= 20）
        for (const auto& p : res["result"]["containers"][0]["placements"])
        {
            int corner = p["x"].get<int>() + p["dx"].get<int>() +
                         p["z"].get<int>() + p["dz"].get<int>();
            REQUIRE(corner <= 20);
        }
    }
}

// 斜面非法输入：截距个数错误 / 越界 / 与已有放置冲突 → status=invalid + 具体信息
TEST_CASE("facet 非法输入返回 invalid", "[solver][facet]")
{
    auto intercepts = load_data("data/tests/test_facet_invalid_intercepts.json");
    auto res = run(intercepts);
    REQUIRE(res["status"] == "invalid");
    bool has_intercepts = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("exactly two non-zero intercepts") != std::string::npos)
        {
            has_intercepts = true;
        }
    }
    REQUIRE(has_intercepts);

    auto bounds = load_data("data/tests/test_facet_invalid_bounds.json");
    res = run(bounds);
    REQUIRE(res["status"] == "invalid");
    bool has_bounds = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("intercept out of container bounds") != std::string::npos)
        {
            has_bounds = true;
        }
    }
    REQUIRE(has_bounds);

    auto resume = load_data("data/tests/test_facet_resume_invalid.json");
    res = run(resume);
    REQUIRE(res["status"] == "invalid");
    bool has_resume = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("violates container facet") != std::string::npos)
        {
            has_resume = true;
        }
    }
    REQUIRE(has_resume);
}
