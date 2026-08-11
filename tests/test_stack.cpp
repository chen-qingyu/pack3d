#include "test_common.hpp"

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
