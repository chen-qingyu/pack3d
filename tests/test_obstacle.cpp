#include "test_common.hpp"

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
