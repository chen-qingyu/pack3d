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

// 续装快照是 locked 容器，sort_stack_weights 不得重排其已有放置。
// 这里刻意放成轻下重上：若跳过 locked 失效，后处理会把两个 z 位置对调。
TEST_CASE("resume locked 容器不被重量排序后处理改动", "[solver][resume]")
{
    auto base = load_data("data/tests/test_resume.json");
    base["container_types"][0]["sz"] = 100;
    // JSON schema 要求 boxes 至少一项，保留 b1；它只用来产生一次完整求解，
    // 断言只检查 locked 柱的已有放置。
    json one_box = base["boxes"][0];
    base["boxes"] = json::array({one_box});

    json lower = base["existing_containers"][0]["placements"][0];
    lower["weight"] = 5.0;
    json upper = lower;
    upper["box_id"] = "a2";
    upper["z"] = 50;
    upper["weight"] = 20.0;
    base["existing_containers"][0]["placements"] = json::array({lower, upper});

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        const auto& placements = res["result"]["containers"][0]["placements"];
        REQUIRE(placements.size() == 3);

        int a1_z = -1;
        int a2_z = -1;
        for (const auto& pl : placements)
        {
            const auto id = pl["box_id"].get<std::string>();
            if (id == "a1")
            {
                a1_z = pl["z"].get<int>();
            }
            else if (id == "a2")
            {
                a2_z = pl["z"].get<int>();
            }
        }
        REQUIRE(a1_z == 0);
        REQUIRE(a2_z == 50);
    }
}
