#include "test_common.hpp"

namespace
{
// 任一容器存在非危险品放置
bool has_regular_placement(const json& container)
{
    for (const auto& pl : container["placements"])
    {
        if (!pl.value("danger", false))
        {
            return true;
        }
    }
    return false;
}

// 任一容器存在危险品放置
bool has_danger_placement(const json& container)
{
    for (const auto& pl : container["placements"])
    {
        if (pl.value("danger", false))
        {
            return true;
        }
    }
    return false;
}
} // namespace

// ===== 危险品三选一输入校验 =====

TEST_CASE("pre_validate_input 检测 danger 三选一：箱型与箱子互斥", "[validation][danger]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.danger = true; // 箱型级
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", {}, true}); // 箱子级也配置 → 互斥
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent danger") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input 检测 danger 三选一：箱型必须全部配置", "[validation][danger]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt1;
    bt1.id = "bt1";
    bt1.size = {100, 100, 100};
    bt1.allowed_orientations = {Orientation::XYZ};
    bt1.danger = true;
    p.box_types.push_back(bt1);
    BoxType bt2;
    bt2.id = "bt2";
    bt2.size = {50, 50, 50};
    bt2.allowed_orientations = {Orientation::XYZ};
    // bt2 未配置 danger → 部分配置非法
    p.box_types.push_back(bt2);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", {}});
    p.boxes.push_back({"box2", "bt2", std::nullopt, "", {}});
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent danger") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input danger 箱型级全部配置通过", "[validation][danger]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt1;
    bt1.id = "bt1";
    bt1.size = {100, 100, 100};
    bt1.allowed_orientations = {Orientation::XYZ};
    bt1.danger = true;
    p.box_types.push_back(bt1);
    BoxType bt2;
    bt2.id = "bt2";
    bt2.size = {50, 50, 50};
    bt2.allowed_orientations = {Orientation::XYZ};
    bt2.danger = false;
    p.box_types.push_back(bt2);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", {}});
    p.boxes.push_back({"box2", "bt2", std::nullopt, "", {}});
    REQUIRE(pre_validate_input(p).empty());
}

// ===== 危险品分柜行为 =====

TEST_CASE("danger 单容器：最后一车混装普货合法", "[solver][danger]")
{
    json input = load_data("data/tests/test_danger.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["result"]["containers"][0]["danger"] == true);
        REQUIRE(has_danger_placement(res["result"]["containers"][0]));
        REQUIRE(has_regular_placement(res["result"]["containers"][0]));
    }
}

TEST_CASE("danger 多容器：仅最后一车混装普货", "[solver][danger]")
{
    json input = load_data("data/tests/test_danger_multi.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 3);
        const auto& containers = res["result"]["containers"];
        // 前两个危险品容器（非最后一车）不得混装普货
        for (int i = 0; i < 2; ++i)
        {
            INFO("container=" << i);
            REQUIRE(containers[i]["danger"] == true);
            REQUIRE_FALSE(has_regular_placement(containers[i]));
        }
        // 最后一车（含危险品的最大索引）允许混装普货
        REQUIRE(containers[2]["danger"] == true);
        REQUIRE(has_regular_placement(containers[2]));
        REQUIRE(has_danger_placement(containers[2]));
    }
}

TEST_CASE("danger 续装：已有危险品容器仅继续装危险品", "[solver][danger]")
{
    json input = load_data("data/tests/test_danger_resume.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 2);
        const auto& containers = res["result"]["containers"];
        // 已有危险品容器只收危险品（不混装普货）
        REQUIRE(containers[0]["danger"] == true);
        REQUIRE_FALSE(has_regular_placement(containers[0]));
        // 普货进入独立容器
        REQUIRE(containers[1]["danger"] == false);
        REQUIRE(has_regular_placement(containers[1]));
    }
}

TEST_CASE("danger 装托：危险品散件单独成托并分柜", "[solver][danger][pallet]")
{
    json input = load_data("data/tests/test_danger_pallet.json");
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        INFO("algo=" << algo);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["pallet_count"] == 2);
        const auto& pallets = res["result"]["pallets"];
        bool has_danger_pallet = false;
        bool has_regular_pallet = false;
        for (const auto& p : pallets)
        {
            if (p.value("danger", false))
            {
                has_danger_pallet = true;
            }
            else
            {
                has_regular_pallet = true;
            }
        }
        REQUIRE(has_danger_pallet);
        REQUIRE(has_regular_pallet);
        // 容器间按危险/普货分柜
        const auto& containers = res["result"]["containers"];
        REQUIRE(containers[0]["danger"] == true);
        REQUIRE(containers[1]["danger"] == false);
    }
}
