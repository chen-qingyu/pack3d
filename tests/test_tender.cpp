#include "core/constraints.hpp"
#include "test_common.hpp"

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

TEST_CASE("tender 多 group 候选按整体检查", "[core][tender]")
{
    TenderState ts;
    ts.limit = 2;
    ts.sizes = {1, 1};
    ts.group_tenders["A"] = {0};
    ts.group_tenders["B"] = {1};

    REQUIRE(check_tender_limit(ts, {}, std::set<std::string>{"A"}));
    REQUIRE(check_tender_limit(ts, {}, std::set<std::string>{"B"}));
    REQUIRE_FALSE(check_tender_limit(ts, {}, std::set<std::string>{"A", "B"}));
}
