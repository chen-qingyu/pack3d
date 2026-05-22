#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace hypercube;

TEST_CASE("目标函数与平台约束集成测试", "[solver]")
{
    std::ifstream ifs("data/tests/test_min_platform.json");
    REQUIRE(ifs.is_open());
    std::stringstream buf;
    buf << ifs.rdbuf();
    auto base = nlohmann::json::parse(buf.str());

    // --- 1) max_avg_volume_rate：一大一小，两个容器均 100%，共 3 个平台 ---
    base["objectives"] = {"max_avg_volume_rate"};
    auto res = run_solver(base.dump());

    REQUIRE(res["summary"]["objective_vector"].size() == 1);
    REQUIRE(res["summary"]["objective_vector"].contains("max_avg_volume_rate"));

    auto& cs = res["result"]["containers"];
    REQUIRE(cs.size() == 2);
    REQUIRE(cs[0]["type_id"] == "big");
    REQUIRE(cs[0]["load_summary"]["volume_rate"] == 1.0);
    REQUIRE(cs[0]["load_summary"]["platforms"].size() == 2);
    REQUIRE(cs[1]["type_id"] == "small");
    REQUIRE(cs[1]["load_summary"]["volume_rate"] == 1.0);
    REQUIRE(cs[1]["load_summary"]["platforms"].size() == 1);

    // --- 2) 默认目标：两个大容器，各一个平台 ---
    base["objectives"] = nlohmann::json::array();
    res = run_solver(base.dump());

    REQUIRE(res["summary"]["objective_vector"].size() == 4);
    REQUIRE(res["result"]["containers"].size() == 2);
    for (const auto& c : res["result"]["containers"])
    {
        REQUIRE(c["type_id"] == "big");
        REQUIRE(c["load_summary"]["platforms"].size() == 1);
    }
}

TEST_CASE("容器数量目标测试", "[solver]")
{
    std::ifstream ifs("data/tests/test_min_container.json");
    REQUIRE(ifs.is_open());
    std::stringstream buf;
    buf << ifs.rdbuf();
    auto base = nlohmann::json::parse(buf.str());

    // --- 默认目标 ---
    auto res = run_solver(base.dump());
    REQUIRE(res["summary"]["objective_vector"].size() == 4);
    REQUIRE(res["result"]["containers"].size() == 1);

    // --- max_avg_volume_rate：两个小容器，各 100% ---
    base["objectives"] = {"max_avg_volume_rate"};
    res = run_solver(base.dump());
    REQUIRE(res["summary"]["objective_vector"].size() == 1);

    auto& cs2 = res["result"]["containers"];
    REQUIRE(cs2.size() == 2);
    REQUIRE(cs2[0]["type_id"] == "small");
    REQUIRE(cs2[0]["load_summary"]["volume_rate"] == 1.0);
    REQUIRE(cs2[1]["type_id"] == "small");
    REQUIRE(cs2[1]["load_summary"]["volume_rate"] == 1.0);
}
