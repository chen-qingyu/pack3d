#include <cstdint>
#include <fstream>
#include <set>
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
    json input = load_data("data/demo.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["result"]["containers"].size() >= 1);
        // 输出不再包含 objective_keys 字段
        REQUIRE(!res["summary"].contains("objective_keys"));
    }
}

// 输出字段恒存在：未启用的功能给合理默认值（null / 空数组 / false）
TEST_CASE("输出字段恒存在", "[solver][output]")
{
    auto res = run(load_data("data/demo.json"));
    REQUIRE(res["status"] == "complete");

    // 顶层
    REQUIRE(res.contains("violations"));
    REQUIRE(res["violations"].is_array());
    REQUIRE(res["result"].contains("pallets"));
    REQUIRE(res["result"]["pallets"].is_array());
    REQUIRE(res["result"].contains("unpacked_boxes"));
    REQUIRE(res["result"]["unpacked_boxes"].is_array());

    // 容器级（demo 无障碍/斜面/站点/tender）
    for (const auto& c : res["result"]["containers"])
    {
        REQUIRE(c.contains("obstacles"));
        REQUIRE(c["obstacles"].is_array());
        REQUIRE(c.contains("facets"));
        REQUIRE(c["facets"].is_array());
        REQUIRE(c.contains("tender"));
        REQUIRE(c["tender"].is_null());
        REQUIRE(c.contains("payload"));
        REQUIRE(c.contains("used_weight"));
        REQUIRE(c.contains("weight_rate"));
        REQUIRE(c.contains("platforms"));
        REQUIRE(c.contains("groups"));
    }

    // 箱型级（demo 无承重/重量/散件）
    for (const auto& bt : res["result"]["box_types"])
    {
        REQUIRE(bt.contains("max_stack"));
        REQUIRE((bt["max_stack"].is_null() || bt["max_stack"].is_array()));
        REQUIRE(bt.contains("max_load"));
        REQUIRE((bt["max_load"].is_null() || bt["max_load"].is_array()));
        REQUIRE(bt.contains("weight"));
        REQUIRE(bt.contains("loose"));
    }

    // 放置级
    for (const auto& c : res["result"]["containers"])
    {
        for (const auto& pl : c["placements"])
        {
            REQUIRE(pl.contains("platform"));
            REQUIRE(pl.contains("group"));
            REQUIRE(pl.contains("weight"));
        }
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
    no_weight_meta["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}, {"weight", 5.0}}}; // 有重量但容器无 payload
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
    json input = load_data("data/tests/test_min_container.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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
    json input = load_data("data/tests/test_min_platform.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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
// 所有排序准则的确定性 pass 在 route 下统一按深度优先稳定排序（深处平台先放）后恢复通过。
TEST_CASE("platform_merge_no_new_container", "[solver]")
{
    json input = load_data("data/tests/test_platform_merge.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);

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
    json input = load_data("data/tests/test_volume_first.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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
    json input = load_data("data/tests/test_group_split.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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

// test_tender_output.json — 4 个已有容器：A{g1,g2} B{g2,g3} C{g3,g4} D{g5}，新箱子 x1{g6}
// 连通分量：A-B-C 一个 tender，D 一个，x1 一个 → tender 序号 [1,1,1,2,3]
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
        REQUIRE(res["result"]["containers"][4]["tender"].get<int>() == 3);
        // 无 group 的容器 platforms/groups 保持 null 语义
        REQUIRE(res["result"]["containers"][0]["groups"].size() == 2);
    }
}

// test_resume.json — 已有容器 ct 已装 a1（50³，地板剩 3 槽位），2 个新箱 b1/b2
// 阶段 B 续塞成功：全部新箱装入已有容器，不开新容器，原有放置保留
TEST_CASE("resume 续塞已有容器成功", "[solver][resume]")
{
    auto input = load_data("data/tests/test_resume.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["packed_box_count"] == 3); // a1 + b1 + b2
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        // 新箱进入已有容器，原有 a1 保留
        const auto& placements = res["result"]["containers"][0]["placements"];
        REQUIRE(placements.size() == 3);
        std::set<std::string> ids;
        for (const auto& pl : placements)
        {
            ids.insert(pl["box_id"].get<std::string>());
        }
        REQUIRE(ids.count("a1") == 1);
        REQUIRE(ids.count("b1") == 1);
        REQUIRE(ids.count("b2") == 1);
        // 重量口径回归：已有 a1(10) + b1(10) + b2(10) = 30（prefill_load 曾丢失已有放置重量）
        REQUIRE(res["result"]["containers"][0]["used_weight"].get<double>() ==
                Catch::Approx(30.0));
    }
}

// test_resume_mid_box.json — 已有容器 ct 地板正中间已装 mid0（20³），4 个新 40×40×20 箱
// 只能装进四个对角地板区域。回归：GLC carve_out_space 旧十字形切割丢失对角空间
// （4 箱全装不下），现 6-slab 完整分解后 4 箱全装下。
TEST_CASE("resume 地板中间放箱不丢对角空间", "[solver][resume]")
{
    auto input = load_data("data/tests/test_resume_mid_box.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["packed_box_count"] == 5); // mid0 + c1..c4
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    }
}
// test_box_type_weight.json — 箱型级重量：bx1=10、bx2=20，箱子均无重量
// 校验通过后箱子重量取自箱型；容器 used_weight = 3×10 + 2×20 = 70
TEST_CASE("箱型级重量解析与输出回显", "[solver][weight]")
{
    auto input = load_data("data/tests/test_box_type_weight.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 5);
        REQUIRE(res["result"]["containers"][0]["used_weight"].get<double>() ==
                Catch::Approx(70.0));
        // 输出回显箱型重量
        bool has_weight = false;
        for (const auto& bt : res["result"]["box_types"])
        {
            if (bt.contains("weight"))
            {
                has_weight = true;
            }
        }
        REQUIRE(has_weight);
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
// base = 合法障碍物配置，测试内改写触发各自非法条件
TEST_CASE("obstacle 非法输入返回 invalid", "[solver][obstacle]")
{
    const auto base = load_data("data/tests/test_obstacle_invalid.json");

    // 两个障碍物互重叠
    auto overlap = base;
    json obstacle = {{"x", 5}, {"y", 0}, {"z", 0}, {"dx", 10}, {"dy", 10}, {"dz", 5}};
    overlap["container_types"][0]["obstacles"].push_back(obstacle);
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

    // 障碍物越界
    auto oob = base;
    oob["container_types"][0]["obstacles"][0]["dx"] = 30;
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

    // 已有放置与障碍物冲突
    auto resume = base;
    resume["existing_containers"] = {
        {{"type_id", "big"},
         {"placements",
          {{{"box_id", "old0"},
            {"box_type_id", "cube"},
            {"x", 0},
            {"y", 0},
            {"z", 0},
            {"orientation", "xyz"}}}}}};
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
// base = 合法斜面配置，测试内改写触发各自非法条件
TEST_CASE("facet 非法输入返回 invalid", "[solver][facet]")
{
    const auto base = load_data("data/tests/test_facet_invalid.json");

    // 截距个数错误（3 个非零）
    auto intercepts = base;
    intercepts["container_types"][0]["facets"][0] = {{"dx", 10}, {"dz", 10}, {"dy", 5}};
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

    // 截距越界
    auto bounds = base;
    bounds["container_types"][0]["facets"][0]["dx"] = 30;
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

    // 已有放置侵入斜面禁区
    auto resume = base;
    resume["existing_containers"] = {
        {{"type_id", "flight"},
         {"placements",
          {{{"box_id", "old0"},
            {"box_type_id", "cube"},
            {"x", 15},
            {"y", 0},
            {"z", 5},
            {"orientation", "xyz"}}}}}};
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

// ============================================================
// 装托（palletizing）测试
// ============================================================

// test_pallet_basic.json — 2 组散件全部装托 + 普通箱子直接装车
TEST_CASE("pallet 基本：全部装托 + 计数口径", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_basic.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    REQUIRE(res["summary"]["palletized_box_count"] == 8);
    REQUIRE(res["summary"]["loose_box_count"] == 4);
    REQUIRE(res["summary"]["packed_box_count"] == 12);
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    REQUIRE(res["result"]["pallets"].size() == 2);
    // 同组不拆托：每托恰一个组
    REQUIRE(res["result"]["pallets"][0]["groups"] == json::array({"A"}));
    REQUIRE(res["result"]["pallets"][1]["groups"] == json::array({"B"}));
    // 容器 packed_count 按原始散箱口径折算（5+3 装托 + 4 散装）
    REQUIRE(res["result"]["containers"][0]["packed_count"] == 12);
    // 虚拟托盘箱型：仅平面旋转 + max_stack=[1,1]，高度含托盘
    bool saw_virtual = false;
    for (const auto& bt : res["result"]["box_types"])
    {
        if (bt["id"] == "pt1200#1")
        {
            saw_virtual = true;
            REQUIRE(bt["allowed_orientations"] == json::array({"xyz", "yxz"}));
            REQUIRE(bt["max_stack"] == json::array({1, 1}));
            REQUIRE(bt["sz"] == 180); // loaded_height 30 + sz 150
        }
    }
    REQUIRE(saw_virtual);
    // 托盘货物重 = 箱重合计（不含自重）
    REQUIRE(res["result"]["pallets"][0]["used_weight"] == 25.0);
    // 容器内托盘单元 box_id == pallet_id
    REQUIRE(res["result"]["containers"][0]["placements"][0]["box_id"] == "pt1200#1");
}

// test_pallet_resume.json — 装托 + 续装：已有 truck 已装 e1（reg 40³），新散件 l1 装托成
// pt#1，与 r1 一起续塞进已有容器；重量口径必须含已有放置（回归 prefill_load 丢重量）
TEST_CASE("pallet 续装：已有放置兼容 + 重量口径", "[solver][pallet][resume]")
{
    auto input = load_data("data/tests/test_pallet_resume.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["container_count"] == 1);
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 1);
    REQUIRE(res["summary"]["packed_box_count"] == 3); // e1 + pt#1(内 l1) + r1
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    const auto& containers = res["result"]["containers"];
    REQUIRE(containers.size() == 1);
    // 已有 e1 保留，托盘与 r1 进入同一容器
    std::set<std::string> ids;
    for (const auto& pl : containers[0]["placements"])
    {
        ids.insert(pl["box_id"].get<std::string>());
    }
    REQUIRE(ids.count("e1") == 1);
    REQUIRE(ids.count("pt#1") == 1);
    REQUIRE(ids.count("r1") == 1);
    // 重量口径：e1(20) + 托盘单元(10) + r1(20) = 50
    REQUIRE(containers[0]["used_weight"].get<double>() == Catch::Approx(50.0));
}

// test_pallet_oversize.json — 散件装不进任何托盘：默认 partial + violation；
// 改写 constraints.pallet_fallback=true → 降级散装 complete
TEST_CASE("pallet oversize/fallback 处理", "[solver][pallet]")
{
    const auto base = load_data("data/tests/test_pallet_oversize.json");

    // 默认（fallback=false）：partial + not palletized
    auto res = run(base);
    REQUIRE(res["status"] == "partial");
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 4);
    REQUIRE(res["summary"]["loose_box_count"] == 2);
    // 未装托散件计入未装箱
    REQUIRE(res["summary"]["unpacked_box_count"] == 1);
    REQUIRE(res["result"]["unpacked_boxes"] == json::array({"big1"}));
    bool found = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("not palletized") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // pallet_fallback=true：未装托散件降级散装 → complete
    auto fb = base;
    fb["constraints"]["pallet_fallback"] = true;
    res = run(fb);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 4);
    REQUIRE(res["summary"]["loose_box_count"] == 3); // 2 普通 + big1 降级
    REQUIRE(res["summary"]["packed_box_count"] == 7);
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    REQUIRE(res["result"]["unpacked_boxes"].empty());
}

// test_pallet_weight.json — 每托不超托盘额定载重
TEST_CASE("pallet 承重不超 payload", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_weight.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 3); // 每托 1 箱（60kg > 50kg 余量）
    REQUIRE(res["summary"]["palletized_box_count"] == 3);
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["used_weight"].get<double>() <= 100.0);
    }
}

// test_pallet_height.json — 堆高不超 max_height，used_height 口径正确
TEST_CASE("pallet 堆高不超 max_height", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_height.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    REQUIRE(res["summary"]["palletized_box_count"] == 30);
    for (const auto& p : res["result"]["pallets"])
    {
        // 装载限高 max_height(450)（不含托盘自身高度）= 450
        REQUIRE(p["used_height"].get<int>() <= 450);
    }
    REQUIRE(res["result"]["pallets"][0]["used_height"] == 400); // 2 层 × 200
}

// test_pallet_group.json — 同组不拆托（软）：无混合托
TEST_CASE("pallet 同组不拆托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_group.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 3); // A(5) B(2) C(1) 各一托
    REQUIRE(res["summary"]["palletized_box_count"] == 8);
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["groups"].size() == 1); // 无混合托
    }
}

// test_pallet_route.json — route 启用时同平台单托，装车后满足路线约束
TEST_CASE("pallet route 同平台单托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_route.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    std::set<std::string> pallet_platforms;
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["platforms"].size() == 1);
        pallet_platforms.insert(p["platforms"][0].get<std::string>());
    }
    REQUIRE(pallet_platforms == std::set<std::string>({"P1", "P2"}));
    // 容器中托盘单元带平台（参与路线约束）
    for (const auto& pl : res["result"]["containers"][0]["placements"])
    {
        if (pl["box_id"].get<std::string>().rfind("pt1200#", 0) == 0)
        {
            REQUIRE(!pl["platform"].is_null());
        }
    }
}

// test_pallet_platform_split.json — 不同站点的货物不能混装一托（混合兜底也按站点分桶）
TEST_CASE("pallet 不同站点不混装一托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_platform_split.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2); // P1/P2 各一托，不混装
    std::set<std::string> pallet_platforms;
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["platforms"].size() == 1);
        pallet_platforms.insert(p["platforms"][0].get<std::string>());
    }
    REQUIRE(pallet_platforms == std::set<std::string>({"P1", "P2"}));
}

// test_pallet_no_stack.json — 装车不叠托（单向）：托盘上无任何箱；托盘可架在普通箱子上方
TEST_CASE("pallet 装车不叠托（单向）", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_no_stack.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    const auto& placements = res["result"]["containers"][0]["placements"];
    const json* pallet_pl = nullptr;
    for (const auto& pl : placements)
    {
        if (pl["box_id"].get<std::string>().rfind("pt1200#", 0) == 0)
        {
            pallet_pl = &pl;
            break;
        }
    }
    REQUIRE(pallet_pl != nullptr);
    // 托盘架在普通箱子（地板 slab）上方
    REQUIRE((*pallet_pl)["z"] == 100);
    const int pallet_top = (*pallet_pl)["z"].get<int>() + (*pallet_pl)["dz"].get<int>();
    // 托盘顶面之上无任何箱子
    for (const auto& pl : placements)
    {
        if (&pl == pallet_pl)
        {
            continue;
        }
        REQUIRE(pl["z"].get<int>() != pallet_top);
    }
}

// test_pallet_invalid.json — 装托输入非法返回 invalid
TEST_CASE("pallet 非法输入返回 invalid", "[solver][pallet]")
{
    const auto base = load_data("data/tests/test_pallet_invalid.json");

    // max_height 非法（0，低于 schema 下限 1）
    auto h = base;
    h["pallet_types"][0]["max_height"] = 0;
    REQUIRE(run(h)["status"] == "invalid");

    // 装托模式强制全重量：箱子缺重量
    auto w = base;
    w["boxes"][0].erase("weight");
    REQUIRE(run(w)["status"] == "invalid");

    // 装托模式强制全重量：容器缺 payload
    auto cw = base;
    cw["container_types"][0].erase("payload");
    REQUIRE(run(cw)["status"] == "invalid");

    // loose 声明但无 pallet_types
    auto np = base;
    np.erase("pallet_types");
    REQUIRE(run(np)["status"] == "invalid");

    // 重复 pallet_type id
    auto dup = base;
    dup["pallet_types"].push_back(dup["pallet_types"][0]);
    REQUIRE(run(dup)["status"] == "invalid");
}

// ============================================================
// 斜面专项回归（2026-08-12）：GLC/BSG 不雕刻斜面、由 check_facet 兜底
// 独立几何断言（8 角点 / AABB 相交判定，不复用实现代码）
// ============================================================

// 放置是否侵入容器任一斜面楔形禁区（8 角点法）
static bool invades_facet(const json& pl, const json& ct)
{
    if (!ct.contains("facets") || ct["facets"].is_null())
    {
        return false;
    }
    const int ext[3] = {ct["sx"].get<int>(), ct["sy"].get<int>(), ct["sz"].get<int>()};
    const int pos[3] = {pl["x"].get<int>(), pl["y"].get<int>(), pl["z"].get<int>()};
    const int size[3] = {pl["dx"].get<int>(), pl["dy"].get<int>(), pl["dz"].get<int>()};
    const char* keys[3] = {"dx", "dy", "dz"};

    for (const auto& f : ct["facets"])
    {
        int inter[3] = {0, 0, 0};
        int u = -1, v = -1;
        for (int a = 0; a < 3; ++a)
        {
            if (f.contains(keys[a]))
            {
                inter[a] = f[keys[a]].get<int>();
                if (inter[a] != 0)
                {
                    if (u < 0)
                    {
                        u = a;
                    }
                    else
                    {
                        v = a;
                    }
                }
            }
        }
        if (u < 0 || v < 0)
        {
            continue; // 防御：预校验保证恰好两个非零截距
        }
        const int64_t mu = (inter[u] < 0) ? -static_cast<int64_t>(inter[u]) : inter[u];
        const int64_t mv = (inter[v] < 0) ? -static_cast<int64_t>(inter[v]) : inter[v];

        // 8 角点：截距轴取最靠禁区一端（正 = max 端、负 = min 端）
        for (int i = 0; i < 8; ++i)
        {
            int corner[3];
            for (int a = 0; a < 3; ++a)
            {
                corner[a] = (inter[a] > 0) ? pos[a] + size[a] : pos[a];
            }
            const int64_t du = (inter[u] > 0) ? (ext[u] - corner[u]) : corner[u];
            const int64_t dv = (inter[v] > 0) ? (ext[v] - corner[v]) : corner[v];
            if (du * mv + dv * mu < mu * mv)
            {
                return true;
            }
        }
    }
    return false;
}

// 放置是否与容器任一障碍物空间相交
static bool overlaps_obstacle(const json& pl, const json& ct)
{
    if (!ct.contains("obstacles") || ct["obstacles"].is_null())
    {
        return false;
    }
    const int p0[3] = {pl["x"].get<int>(), pl["y"].get<int>(), pl["z"].get<int>()};
    const int p1[3] = {p0[0] + pl["dx"].get<int>(), p0[1] + pl["dy"].get<int>(),
                       p0[2] + pl["dz"].get<int>()};
    for (const auto& o : ct["obstacles"])
    {
        const int o0[3] = {o["x"].get<int>(), o["y"].get<int>(), o["z"].get<int>()};
        const int o1[3] = {o0[0] + o["dx"].get<int>(), o0[1] + o["dy"].get<int>(),
                           o0[2] + o["dz"].get<int>()};
        if (p0[0] < o1[0] && o0[0] < p1[0] &&
            p0[1] < o1[1] && o0[1] < p1[1] &&
            p0[2] < o1[2] && o0[2] < p1[2])
        {
            return true;
        }
    }
    return false;
}

// 断言结果中所有放置不侵入斜面、不重叠障碍物
static void require_no_geom_violation(const json& res, const json& ct)
{
    for (const auto& c : res["result"]["containers"])
    {
        for (const auto& pl : c["placements"])
        {
            INFO("box=" << pl["box_id"]);
            REQUIRE(!invades_facet(pl, ct));
            REQUIRE(!overlaps_obstacle(pl, ct));
        }
    }
}

// test_facet_multi.json — 多斜面（正/负截距）：平行 Y {dx:10,dz:10} + 平行 X {dy:-8,dz:5}
// 验证：四算法均不崩溃、所有放置不侵入任一楔形（check_facet 兜底对多斜面/负截距正确）
TEST_CASE("facet 多斜面与负截距：四算法无侵入", "[solver][facet]")
{
    auto input = load_data("data/tests/test_facet_multi.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 8);
        require_no_geom_violation(res, input["container_types"][0]);
    }
}

// test_facet_resume.json — 斜面 + 续装：已有 2 放置（c0/c1）+ 8 待装
// 验证：已有放置保留、新增不侵入斜面、10 箱全装下
TEST_CASE("facet 续装：已有放置保留、新增不侵入", "[solver][facet]")
{
    auto input = load_data("data/tests/test_facet_resume.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 10);
        require_no_geom_violation(res, input["container_types"][0]);

        std::set<std::string> packed;
        for (const auto& c : res["result"]["containers"])
        {
            for (const auto& pl : c["placements"])
            {
                packed.insert(pl["box_id"].get<std::string>());
            }
        }
        REQUIRE(packed.count("c0") == 1);
        REQUIRE(packed.count("c1") == 1);
    }
}

// test_facet_combo.json — 斜面 + 障碍物 + 支撑率 0.6
// 验证：BSG 逐叶门控（facets+obstacles+support_rate）下不侵入、不重叠、全装下
TEST_CASE("facet 斜面+障碍物+支撑率组合", "[solver][facet]")
{
    auto input = load_data("data/tests/test_facet_combo.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 8);
        require_no_geom_violation(res, input["container_types"][0]);
    }
}

// test_facet_origin.json — 两负截距 {dx:-10,dz:-10}，楔形禁区覆盖原点 (0,0,0)
// 验证：初始原点不可用时四算法均有备用起点（GEP/RGS 地板扫描、GLC/BSG 贴角雕刻），
// 全部 8 箱装下且不侵入（无备用起点会零装载）
TEST_CASE("facet 禁区覆盖原点：四算法仍可装载", "[solver][facet]")
{
    auto input = load_data("data/tests/test_facet_origin.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["packed_box_count"] == 8);
        require_no_geom_violation(res, input["container_types"][0]);
    }
}
