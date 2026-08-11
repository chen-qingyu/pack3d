#include "test_common.hpp"

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
