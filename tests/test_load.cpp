#include "test_common.hpp"

// 承重（max_stack / max_load，含非均匀重量）与重量解析（箱型级/箱子级）约束场景

// 路线角落场景：单层车厢 300x200x100（高=箱高，禁止堆叠），6 个地面槽位。
// P2（深处）4 箱先放占深+中列，P1（门）2 箱后放须落入剩余门列（X=200 的 100x200x100 空区）。
// 最优 1 车 = 同车混装 P1/P2。回归：GLC 曾因局部评分 platform_split 优先，
// 跨平台续装被"多目标提前停止"误判为更差而整桶放弃，只能 2 车。
TEST_CASE("route 角落混装 1 车", "[solver][route]")
{
    json input = load_data("data/tests/test_route_corner.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["result"]["containers"][0]["placements"].size() == 6);
    }
}

// 续装（existing_containers）跨平台：已有容器放了 4 个 P2（占深+中列），
// 剩门列（X=200 的 100x200x100）应继续放入 2 个 P1，不新开容器。
// 回归：GLC 的"多目标提前停止"曾在已有放置非空时拒放跨平台块 → 只能新开容器。
TEST_CASE("route 续装跨平台填满余空", "[solver][route]")
{
    json input = load_data("data/tests/test_resume_platform.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["result"]["containers"][0]["placements"].size() == 6);
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

// 重不压轻（heavy_not_on_light）：任一直接支撑关系不得"上重下轻"。
// 场景 200x100x300：h1(30)+l1(10)+l2(10)，重箱必须在下层，轻箱可叠上。
TEST_CASE("heavy_not_on_light 重不压轻", "[solver][stack]")
{
    json input = load_data("data/tests/test_heavy_not_on_light.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        // 校验整批输出：任何直接支撑关系都不存在"上重下轻"
        for (const auto& c : res["result"]["containers"])
        {
            const auto& ps = c["placements"];
            for (size_t a = 0; a < ps.size(); ++a)
            {
                const auto& A = ps[a];
                const int az = A["z"].get<int>();
                const int ax1 = A["x"].get<int>();
                const int ax2 = ax1 + A["dx"].get<int>();
                const int ay1 = A["y"].get<int>();
                const int ay2 = ay1 + A["dy"].get<int>();
                const double aw = A["weight"].get<double>();
                for (size_t b = 0; b < ps.size(); ++b)
                {
                    if (a == b)
                    {
                        continue;
                    }
                    const auto& B = ps[b];
                    if (az + A["dz"].get<int>() != B["z"].get<int>())
                    {
                        continue;
                    }
                    const int bx1 = B["x"].get<int>();
                    const int bx2 = bx1 + B["dx"].get<int>();
                    const int by1 = B["y"].get<int>();
                    const int by2 = by1 + B["dy"].get<int>();
                    if (bx2 <= ax1 || ax2 <= bx1 || by2 <= ay1 || ay2 <= by1)
                    {
                        continue;
                    }
                    REQUIRE(B["weight"].get<double>() <= aw + 1e-9);
                }
            }
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
