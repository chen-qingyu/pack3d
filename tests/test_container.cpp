#include "test_common.hpp"

// 容器类型级空间特征：障碍物（obstacle）与斜面（facet）楔形禁区
// 几何断言独立于实现（8 角点 / AABB 相交判定）

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

        // 8 角点：截距轴取最靠禁区一端（负 = max 端、正 = min 端）
        for (int i = 0; i < 8; ++i)
        {
            int corner[3];
            for (int a = 0; a < 3; ++a)
            {
                corner[a] = (inter[a] < 0) ? pos[a] + size[a] : pos[a];
            }
            const int64_t du = (inter[u] < 0) ? (ext[u] - corner[u]) : corner[u];
            const int64_t dv = (inter[v] < 0) ? (ext[v] - corner[v]) : corner[v];
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

// test_facet_chamfer.json — 顶前 45° 斜切 {dx:-10,dz:-10}（x+z>20 为楔形禁区）
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
        REQUIRE(res["result"]["containers"][0]["facets"][0]["dx"] == -10);
        REQUIRE(res["result"]["containers"][0]["facets"][0]["dz"] == -10);
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

// test_facet_multi.json — 多斜面（正/负截距）：平行 Y {dx:-10,dz:-10} + 平行 X {dy:8,dz:-5}
// 验证：四算法均不崩溃、所有放置不侵入任一楔形（check_facet 兜底对多斜面与正负截距正确）
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

// test_facet_origin.json — 两正截距 {dx:10,dz:10}，楔形禁区覆盖原点 (0,0,0)
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
