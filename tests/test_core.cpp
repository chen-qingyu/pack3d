#include <algorithm>
#include <array>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/algorithm/glc/space.hpp"
#include "core/constraints.hpp"
#include "core/objectives.hpp"
#include "core/types.hpp"

using namespace pack3d;

TEST_CASE("Size::orient 产生正确的尺寸", "[core]")
{
    Size base{500, 400, 300};

    auto r1 = base.orient(Orientation::XYZ);
    REQUIRE(r1.dx == 500);
    REQUIRE(r1.dy == 400);
    REQUIRE(r1.dz == 300);

    auto r2 = base.orient(Orientation::YXZ);
    REQUIRE(r2.dx == 400);
    REQUIRE(r2.dy == 500);
    REQUIRE(r2.dz == 300);

    auto r3 = base.orient(Orientation::ZYX);
    REQUIRE(r3.dx == 300);
    REQUIRE(r3.dy == 400);
    REQUIRE(r3.dz == 500);
}

TEST_CASE("check_boundary 拒绝越界", "[core]")
{
    ContainerType ct;
    ct.inner_size = {1000, 1000, 1000};
    ct.payload = 1000.0;

    REQUIRE(check_boundary(ct, {0, 0, 0}, {500, 500, 500}));
    REQUIRE(check_boundary(ct, {500, 500, 500}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {501, 0, 0}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {0, -1, 0}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {0, 0, 0}, {1001, 500, 500}));
}

TEST_CASE("check_overlap 检测碰撞", "[core]")
{
    std::vector<Placement> exists = {{"", "t", "", {50, 50, 50}, Orientation::XYZ, {100, 100, 100}}};

    REQUIRE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));

    exists[0].position = {200, 0, 0};
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));

    exists[0].position = {0, 200, 0};
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));
}

TEST_CASE("check_support 在地板上总是通过", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, 1.0));
    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, 0.0));
}

TEST_CASE("check_support 部分支撑", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}, 10000.0};
    load.type = &ct;

    load.placements.push_back({"", "bt1", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    load.used_volume = 100 * 100 * 100;
    load.total_weight = 100.0;

    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, 0.0));
    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, 1.0));
    REQUIRE(check_support({0, 0, 100}, {200, 100, 100}, load, 0.5));
    REQUIRE_FALSE(check_support({0, 0, 100}, {200, 100, 100}, load, 0.6));
}

// 四角均被支撑但面积占比不足时，高 support_rate 必须拒绝（回归：删除四角快通道后）
TEST_CASE("check_support 四角支撑但面积不足被拒绝", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    // 4 个 10x10 角块在四角，顶面 z=50
    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {90, 0, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {0, 90, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {90, 90, 0}, Orientation::XYZ, {10, 10, 50}});

    // 100x100 大箱：四角全被支撑，但支撑面积仅 4%
    REQUIRE_FALSE(check_support({0, 0, 50}, {100, 100, 100}, load, 0.9));
    REQUIRE(check_support({0, 0, 50}, {100, 100, 100}, load, 0.03));
}

// 零厚膜（某维为 0）只拦截"严格横跨该平面"的箱子；贴面（任一侧）均放行
TEST_CASE("check_obstacle 零厚膜只拦横跨", "[core]")
{
    const std::vector<Obstacle> obs = {{10, 0, 0, 0, 20, 20}}; // 垂直墙膜 x=10

    // 横跨：pos.x < 10 < pos.x+dx → 拦截
    REQUIRE(check_obstacle({0, 0, 0}, {15, 5, 5}, obs));
    // 贴面左（右边界恰 x=10）/ 贴面右（左边界恰 x=10）→ 放行
    REQUIRE_FALSE(check_obstacle({0, 0, 0}, {10, 5, 5}, obs));
    REQUIRE_FALSE(check_obstacle({10, 0, 0}, {5, 5, 5}, obs));
    // 完全位于任一侧 → 放行
    REQUIRE_FALSE(check_obstacle({0, 0, 0}, {5, 5, 5}, obs));
    REQUIRE_FALSE(check_obstacle({15, 0, 0}, {5, 5, 5}, obs));
}

// 零厚膜为纯穿越拦截，不参与支撑；实体障碍物顶面仍等价地板
TEST_CASE("check_support 零厚膜不参与支撑", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {20, 10, 10}};
    ct.obstacles = {{0, 0, 5, 20, 10, 0}}; // 水平搁板膜 z=5（dz=0）
    load.type = &ct;

    // 搁板膜不支撑：底面 z=5 的箱无其它支撑 → 高支撑率下拒绝
    REQUIRE_FALSE(check_support({0, 0, 5}, {5, 5, 5}, load, 1.0));

    // 对比：实体台阶顶面作为支撑面可承托（证明膜被特殊跳过）
    ContainerType ct2{{}, {20, 10, 10}};
    ct2.obstacles = {{0, 0, 0, 20, 10, 5}}; // 实体台阶，顶面 z=5
    load.type = &ct2;
    REQUIRE(check_support({0, 0, 5}, {5, 5, 5}, load, 1.0));
}

TEST_CASE("平台数量限制约束", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{};
    ct.inner_size = {1000, 1000, 1000};
    ct.payload = 10000.0;
    load.type = &ct;
    load.platforms.insert("A");

    REQUIRE_FALSE(check_platform_limit(load, "B", 1));
    REQUIRE(check_platform_limit(load, "A", 1));
    REQUIRE(check_platform_limit(load, "B", 0));
}

TEST_CASE("路线约束：X 重叠但 Y 分隔允许", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // 先放 A（近门）到 (0,0,0)
    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});

    // B 的 X 与 A 重叠，但 Y 完全分离 → 允许
    REQUIRE(check_route_order(load, "B", {0, 200, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：X 重叠但 Z 分隔允许", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});

    // B 与 A 在 Z 上完全分离（B 在上方但未直接叠压）→ 允许
    REQUIRE(check_route_order(load, "B", {0, 0, 200}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：YZ 重叠时先卸平台必须在近门处", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // B（后卸）在深处 (200,0,0)
    load.placements.push_back({"", "", "", {200, 0, 0}, Orientation::XYZ, {100, 100, 100}, "B"});

    // A 在 YZ 上与 B 重叠，但 X 在 B 左侧（深处）→ 拒绝（A 应先卸，必须在近门处）
    REQUIRE_FALSE(check_route_order(load, "A", {0, 0, 0}, {100, 100, 100}, route));
    // A 完全在 B 右侧（近门）→ 允许
    REQUIRE(check_route_order(load, "A", {300, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：XY 重叠时后卸平台不能压住先卸平台", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先卸）在 (0, 0, 100)，B（后卸）在 (0, 0, 0)
    // XY 重叠，B.z=0 ≤ A.z=100 → B 没压住 A → 允许
    load.placements.push_back({"", "", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 50}, "A"});
    REQUIRE(check_route_order(load, "B", {0, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：后卸平台压住先卸平台被拒绝", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先卸）在 (0,0,0) 高 100，B（后卸）在 (0,0,100) 直接叠在 A 上
    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});
    // B 直接压在 A 上方 → 拒绝
    REQUIRE_FALSE(check_route_order(load, "B", {0, 0, 100}, {100, 100, 50}, route));
}

TEST_CASE("ObjectiveVector 字典序比较", "[core]")
{
    ObjectiveVector a{2, 3, 0.8, 5};
    ObjectiveVector b{2, 3, 0.8, 5};
    REQUIRE(a == b);
    REQUIRE_FALSE(a.is_better_than(b));

    ObjectiveVector fewer_containers{1, 2, 0.9, 3};
    REQUIRE(fewer_containers.is_better_than(a));

    ObjectiveVector fewer_platforms{2, 2, 0.8, 5};
    REQUIRE(fewer_platforms.is_better_than(a));

    ObjectiveVector higher_rate{2, 3, 0.9, 5};
    REQUIRE(higher_rate.is_better_than(a));
}

namespace
{
BoxType make_stack_bt(const std::string& id, std::optional<int> max_stack,
                      std::optional<double> max_load = std::nullopt)
{
    BoxType bt;
    bt.id = id;
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    if (max_stack.has_value())
    {
        bt.max_stack = {max_stack.value()};
    }
    if (max_load.has_value())
    {
        bt.max_load = {max_load.value()};
    }
    return bt;
}

ContainerLoad make_stack_load()
{
    static ContainerType ct{{}, {1000, 1000, 1000}};
    ContainerLoad load;
    load.type_id = "t";
    load.type = &ct;
    return load;
}
} // namespace

TEST_CASE("max_stack: 同型柱限层", "[core][stack]")
{
    auto bt = make_stack_bt("bt", 2);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 1);

    // 第二层 level 2 <= max_stack 2
    REQUIRE(check_stack_constraints({0, 0, 100}, {100, 100, 100}, "bt", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 2);

    // 第三层 level 3 > max_stack 2 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, "bt", Orientation::XYZ, 0.0, load, btm));
}

TEST_CASE("max_stack: 同箱型连续层数，异型不互计", "[core][stack]")
{
    auto strong = make_stack_bt("strong", 5);
    auto weak = make_stack_bt("weak", 2);
    std::map<std::string, BoxType> btm = {{"strong", strong}, {"weak", weak}};
    auto load = make_stack_load();

    load.placements.push_back({"", "strong", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);

    // weak 放 strong 上：异型，weak 起新 run（same_run=1 <= 2）→ 通过
    REQUIRE(check_stack_constraints({0, 0, 100}, {100, 100, 100}, "weak", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "weak", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 2);

    // strong 放 weak 上：异型，weak 不限制 strong（same_run=1 <= 5）→ 放行（A 关键点）
    REQUIRE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, "strong", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "strong", "", {0, 0, 200}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 200}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 3);

    // weak 放 strong 上：异型，weak 新 run（same_run=1 <= 2）→ 放行
    REQUIRE(check_stack_constraints({0, 0, 300}, {100, 100, 100}, "weak", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "weak", "", {0, 0, 300}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 300}, {100, 100, 100}, 0.0, load);

    // weak 连续压在 weak(level4) 上：same_run=2 <= 2 → 放行
    REQUIRE(check_stack_constraints({0, 0, 400}, {100, 100, 100}, "weak", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "weak", "", {0, 0, 400}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 400}, {100, 100, 100}, 0.0, load);

    // weak 再压（level6）：same_run=3 > 2 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 500}, {100, 100, 100}, "weak", Orientation::XYZ, 0.0, load, btm));
}

TEST_CASE("max_load: A3 面积分摊（跨型直接支撑）", "[core][stack]")
{
    BoxType base;
    base.id = "base";
    base.size = {200, 200, 100};
    base.allowed_orientations = {Orientation::XYZ};
    base.max_load = {100.0};
    BoxType top;
    top.id = "top";
    top.size = {200, 100, 100};
    top.allowed_orientations = {Orientation::XYZ};
    std::map<std::string, BoxType> btm = {{"base", base}, {"top", top}};
    auto load = make_stack_load();

    // 底座 200x200，max_load 100：半面积覆盖只分配 100×20000/40000=50 容量
    load.placements.push_back({"", "base", "", {0, 0, 0}, Orientation::XYZ, {200, 200, 100}});
    apply_stack_state({0, 0, 0}, {200, 200, 100}, 0.0, load);

    // top 200x100 w100 占半面积（跨型）：A3 份额 100 > 分配 50 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 100.0, load, btm));
    // top 200x100 w40 占半面积：份额 40 <= 分配 50 → 通过
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 40.0, load, btm));
    load.placements.push_back({"", "top", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 40.0, load);
    REQUIRE(load.placements[0].cum_load == Catch::Approx(40.0));
    REQUIRE(load.placements[0].has_cross_above);
    // 并排第二个 top w40（y=100）：A3 通过，base 整柱累计 80 <= 100 → 通过
    REQUIRE(check_stack_constraints({0, 100, 100}, {200, 100, 100}, "top", Orientation::XYZ, 40.0, load, btm));
    load.placements.push_back({"", "top", "", {0, 100, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 100, 100}, {200, 100, 100}, 40.0, load);
    REQUIRE(load.placements[0].cum_load == Catch::Approx(80.0));
    // 第三个 top w40：base 整柱累计 120 > 100 → 拒绝（D 整柱累计）
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 40.0, load, btm));
}

TEST_CASE("max_load: A3 全覆盖分配全容量（跨型）", "[core][stack]")
{
    BoxType base;
    base.id = "base";
    base.size = {100, 100, 100};
    base.allowed_orientations = {Orientation::XYZ};
    base.max_load = {100.0};
    BoxType top;
    top.id = "top";
    top.size = {200, 100, 100};
    top.allowed_orientations = {Orientation::XYZ};
    std::map<std::string, BoxType> btm = {{"base", base}, {"top", top}};
    auto load = make_stack_load();

    // 底座 100x100
    load.placements.push_back({"", "base", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    // top 200x100 半悬空，但完全覆盖底座顶面 → A3 分配 100% 容量 → 通过
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 100.0, load, btm));
    load.placements.push_back({"", "top", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 100.0, load);
    REQUIRE(load.placements[0].cum_load == Catch::Approx(100.0));
    // 再来一个 100kg：base 整柱累计 200 > max_load 100 → 拒绝（D）
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 100.0, load, btm));
}

TEST_CASE("max_load: 两支撑各 50%（A3 逐对分摊，跨型）", "[core][stack]")
{
    BoxType base;
    base.id = "base";
    base.size = {100, 100, 100};
    base.allowed_orientations = {Orientation::XYZ};
    base.max_load = {40.0};
    BoxType top;
    top.id = "top";
    top.size = {200, 100, 100};
    top.allowed_orientations = {Orientation::XYZ};
    std::map<std::string, BoxType> btm = {{"base", base}, {"top", top}};
    auto load = make_stack_load();

    load.placements.push_back({"", "base", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    load.placements.push_back({"", "base", "", {100, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({100, 0, 0}, {100, 100, 100}, 0.0, load);

    // top 200x100 w100 各占 50%：各支撑分配容量 40 < 份额 50 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 100.0, load, btm));
    // w60：份额 30 <= 分配 40 → 通过，各支撑累计 30
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, "top", Orientation::XYZ, 60.0, load, btm));
    load.placements.push_back({"", "top", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 60.0, load);
    REQUIRE(load.placements[0].cum_load == Catch::Approx(30.0));
    REQUIRE(load.placements[1].cum_load == Catch::Approx(30.0));
}

TEST_CASE("max_load: 面积重叠比例分配承重（用户示例）", "[core][stack]")
{
    // lower 20x20 max_load 10，upper 10x10 w5 全压其上：分配 10×100/400=2.5 < 5 → 拒绝
    BoxType lower;
    lower.id = "lower";
    lower.size = {20, 20, 10};
    lower.allowed_orientations = {Orientation::XYZ};
    lower.max_load = {10.0};
    std::map<std::string, BoxType> btm = {{"lower", lower}};
    auto load = make_stack_load();

    load.placements.push_back({"", "lower", "", {0, 0, 0}, Orientation::XYZ, {20, 20, 10}});
    apply_stack_state({0, 0, 0}, {20, 20, 10}, 0.0, load);

    // 5 > 2.5 → 违反承重
    REQUIRE_FALSE(check_stack_constraints({0, 0, 10}, {10, 10, 10}, "upper", Orientation::XYZ, 5.0, load, btm));
    // 轻箱 w2 <= 2.5 → 通过
    REQUIRE(check_stack_constraints({0, 0, 10}, {10, 10, 10}, "upper", Orientation::XYZ, 2.0, load, btm));
}

TEST_CASE("max_load: 整柱累计承重（D）", "[core][stack]")
{
    // 底层 weak max_load=7，中层/顶层无 max_load；柱高 3 每箱 w5
    // 顶层压上时底层整柱累计 5+5=10 > 7 → 拒绝
    BoxType weak;
    weak.id = "weak";
    weak.size = {100, 100, 100};
    weak.allowed_orientations = {Orientation::XYZ};
    weak.max_load = {7.0};
    BoxType plain;
    plain.id = "plain";
    plain.size = {100, 100, 100};
    plain.allowed_orientations = {Orientation::XYZ};
    std::map<std::string, BoxType> btm = {{"weak", weak}, {"plain", plain}};
    auto load = make_stack_load();

    load.placements.push_back({"b1", "weak", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    load.placements.push_back({"b2", "plain", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 5.0, load);
    // 顶层 w5：底层整柱累计 5+5=10 > 7 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, "plain", Orientation::XYZ, 5.0, load, btm));
    // 底层 max_load=10 时 10 <= 10 → 通过
    weak.max_load = {10.0};
    std::map<std::string, BoxType> btm2 = {{"weak", weak}, {"plain", plain}};
    REQUIRE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, "plain", Orientation::XYZ, 5.0, load, btm2));
}

TEST_CASE("max_stack: 跨不同高度支撑，同型按 run 计数", "[core][stack]")
{
    // S1 弱箱 max_stack=2 在地板（z 顶=100）；强柱 A->B2->S2（max_stack=5）顶面同为 z=100。
    // B(strong) 同时搭在 S1 与 S2 上：B 与 S2 同型，run = S2.same_run+1 = 4 <= 5；S1 异型不计数 → 放行
    BoxType weak;
    weak.id = "weak";
    weak.size = {100, 100, 100};
    weak.allowed_orientations = {Orientation::XYZ};
    weak.max_stack = {2};
    BoxType strong;
    strong.id = "strong";
    strong.size = {100, 100, 100};
    strong.allowed_orientations = {Orientation::XYZ};
    strong.max_stack = {5};
    std::map<std::string, BoxType> btm = {{"weak", weak}, {"strong", strong}};
    auto load = make_stack_load();

    load.placements.push_back({"S1", "weak", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    load.placements.push_back({"A", "strong", "", {100, 0, 0}, Orientation::XYZ, {100, 100, 30}});
    apply_stack_state({100, 0, 0}, {100, 100, 30}, 0.0, load);
    load.placements.push_back({"B2", "strong", "", {100, 0, 30}, Orientation::XYZ, {100, 100, 30}});
    apply_stack_state({100, 0, 30}, {100, 100, 30}, 0.0, load);
    load.placements.push_back({"S2", "strong", "", {100, 0, 60}, Orientation::XYZ, {100, 100, 40}});
    apply_stack_state({100, 0, 60}, {100, 100, 40}, 0.0, load);

    // B(strong) 跨 S1（weak）与 S2（strong）：同型 run 与 S2 连续 → 放行
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 50}, "strong", Orientation::XYZ, 0.0, load, btm));

    load.placements.push_back({"B", "strong", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 50}});
    apply_stack_state({0, 0, 100}, {200, 100, 50}, 0.0, load);
    auto run_of = [&](const std::string& id)
    {
        for (const auto& pl : load.placements)
        {
            if (pl.box_id == id)
            {
                return pl.same_run;
            }
        }
        return -1;
    };
    REQUIRE(load.placements.back().stack_level == 4);
    REQUIRE(run_of("S1") == 1); // weak 单独，不被 strong 计数
    REQUIRE(run_of("S2") == 3); // A+B2+S2
    REQUIRE(run_of("B2") == 2);
    REQUIRE(run_of("A") == 1);
    REQUIRE(run_of("B") == 4); // B 接到 S2 的 strong run

    // 对照组 1：S1 max_stack=1，B 若为 strong（异型）→ 仍放行（weak 只限 weak）
    BoxType strongB = strong;
    BoxType weak1 = weak;
    weak1.max_stack = {1};
    std::map<std::string, BoxType> btm1 = {{"weak", weak1}, {"strong", strongB}};
    auto load1 = make_stack_load();
    load1.placements.push_back({"S1", "weak", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load1);
    load1.placements.push_back({"A", "strong", "", {100, 0, 0}, Orientation::XYZ, {100, 100, 30}});
    apply_stack_state({100, 0, 0}, {100, 100, 30}, 0.0, load1);
    load1.placements.push_back({"B2", "strong", "", {100, 0, 30}, Orientation::XYZ, {100, 100, 30}});
    apply_stack_state({100, 0, 30}, {100, 100, 30}, 0.0, load1);
    load1.placements.push_back({"S2", "strong", "", {100, 0, 60}, Orientation::XYZ, {100, 100, 40}});
    apply_stack_state({100, 0, 60}, {100, 100, 40}, 0.0, load1);
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 50}, "strong", Orientation::XYZ, 0.0, load1, btm1));

    // 对照组 2：B 若为 weak（与 S1 同型）且 S1 max_stack=1 → run=2 > 1 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 50}, "weak", Orientation::XYZ, 0.0, load1, btm1));
}

TEST_CASE("max_stack: 同层并排不改同型 run", "[core][stack]")
{
    auto bt = make_stack_bt("bt", 2);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    // 底座 300x100，max_stack 2
    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {300, 100, 100}});
    apply_stack_state({0, 0, 0}, {300, 100, 100}, 0.0, load);
    // 同层并排两个 100x100：各自 run = 底座.same_run+1 = 2 <= 2 → 均通过
    REQUIRE(check_stack_constraints({0, 0, 100}, {100, 100, 100}, "bt", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(check_stack_constraints({100, 0, 100}, {100, 100, 100}, "bt", Orientation::XYZ, 0.0, load, btm));
    load.placements.push_back({"", "bt", "", {100, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({100, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements[0].same_run == 1); // 底座自身 run=1
    REQUIRE(load.placements[1].same_run == 2); // 第一个上层箱 run=2
    REQUIRE(load.placements[2].same_run == 2); // 并排第二个 run=2（不因并排变 3）
    // 第三层叠在其中一个上：run=3 > max_stack 2 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, "bt", Orientation::XYZ, 0.0, load, btm));
}

TEST_CASE("heavy_not_on_light: 上箱重量不得超过直接支撑箱", "[core][stack]")
{
    auto load = make_stack_load();

    // 支撑箱 20kg
    load.placements.push_back({"", "heavy", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "", 20.0});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 20.0, load);
    // 轻箱 10kg 压上：允许
    REQUIRE(check_heavy_not_on_light({0, 0, 100}, {100, 100, 100}, 10.0, load));
    // 等重 20kg：允许
    REQUIRE(check_heavy_not_on_light({0, 0, 100}, {100, 100, 100}, 20.0, load));
    // 重箱 30kg 压上：拒绝
    REQUIRE_FALSE(check_heavy_not_on_light({0, 0, 100}, {100, 100, 100}, 30.0, load));
}

TEST_CASE("recompute_stack_state: 乱序重建", "[core][stack]")
{
    auto bt = make_stack_bt("bt", 3, 200.0);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    // 乱序 push：顶层 b3、底层 b1、中层 b2，各重 50
    load.placements.push_back({"b3", "bt", "", {0, 0, 200}, Orientation::XYZ, {100, 100, 100}, "", 50.0});
    load.placements.push_back({"b1", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "", 50.0});
    load.placements.push_back({"b2", "bt", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}, "", 50.0});

    std::vector<std::string> errs;
    recompute_stack_state(load, btm, &errs);
    REQUIRE(errs.empty());

    auto find_level = [&](const std::string& id)
    {
        for (const auto& pl : load.placements)
        {
            if (pl.box_id == id)
            {
                return std::make_pair(pl.stack_level, pl.cum_load);
            }
        }
        return std::make_pair(0, 0.0);
    };
    // 整柱累计：b1 承受 b2+b3 = 100，b2 承受 b3 = 50
    REQUIRE(find_level("b1") == std::make_pair(1, 100.0));
    REQUIRE(find_level("b2") == std::make_pair(2, 50.0));
    REQUIRE(find_level("b3") == std::make_pair(3, 0.0));
}

TEST_CASE("recompute_stack_state: 检测违例", "[core][stack]")
{
    // b1 max_stack=1（同型不可叠放），同型 b1 压在其上 → 同型 run=2 > 1 → 违例
    auto b1 = make_stack_bt("b1", 1);
    std::map<std::string, BoxType> btm = {{"b1", b1}};
    auto load = make_stack_load();

    load.placements.push_back({"b2", "b1", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    load.placements.push_back({"b1", "b1", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});

    std::vector<std::string> errs;
    recompute_stack_state(load, btm, &errs);
    REQUIRE_FALSE(errs.empty());
    bool found = false;
    for (const auto& e : errs)
    {
        if (e.find("max_stack") != std::string::npos && e.find("b2") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 续装兜底路径：地板正中间放箱后，剩余空间必须完整覆盖（旧十字形切割会丢对角空间）
TEST_CASE("glc carve_out_space 完整覆盖剩余空间", "[core][glc]")
{
    using namespace pack3d::glc;

    Space root;
    root.pos = {0, 0, 0};
    root.lx = 100;
    root.ly = 100;
    root.lz = 50;
    root.id = 1;
    root.parent_id = -1;
    root.kind = SpaceKind::Root;

    // 地板正中间放 20×20×20 箱
    Placement pl;
    pl.position = {40, 40, 0};
    pl.osize = {20, 20, 20};

    std::vector<Space> stack;
    carve_out_space(root, pl, stack);

    // 体积守恒：互不相交的完整分解，总和 = space - box
    int64_t total = 0;
    for (const auto& s : stack)
    {
        total += static_cast<int64_t>(s.lx) * s.ly * s.lz;
    }
    REQUIRE(total == 100LL * 100 * 50 - 20LL * 20 * 20);

    // 四个对角地板区域（旧十字形切割全部丢失）必须可被某空间完整容纳
    auto can_hold = [](const Space& s, int32_t x, int32_t y, int32_t z,
                       int32_t dx, int32_t dy, int32_t dz)
    {
        return s.pos.x <= x && s.pos.y <= y && s.pos.z <= z &&
               s.pos.x + s.lx >= x + dx &&
               s.pos.y + s.ly >= y + dy &&
               s.pos.z + s.lz >= z + dz;
    };
    const std::array<Position, 4> diag = {Position{0, 0, 0}, Position{60, 0, 0},
                                          Position{0, 60, 0}, Position{60, 60, 0}};
    for (const auto& p : diag)
    {
        REQUIRE(std::any_of(stack.begin(), stack.end(), [&](const Space& s)
                            { return can_hold(s, p.x, p.y, p.z, 40, 40, 20); }));
    }
}

TEST_CASE("usable_volume_x: 障碍物按 X 向截断", "[core]")
{
    ContainerType ct;
    ct.inner_size = {10, 10, 10};
    ct.obstacles = {{4, 0, 0, 2, 10, 10}}; // 挡住 x∈[4,6] 全宽高

    ContainerLoad load;
    load.type = &ct;
    load.placements = {
        {"a", "t", "", {0, 0, 0}, Orientation::XYZ, {3, 10, 10}},
        {"b", "t", "", {6, 0, 0}, Orientation::XYZ, {3, 10, 10}},
    };
    load.used_volume = 600;

    REQUIRE(load.used_x() == 9);
    // slab = 9·10·10 = 900，障碍物在 slab 内 x∈[4,6] → 200，可用 700
    REQUIRE(load.usable_volume_x() == 700);
    REQUIRE(load.volume_rate_x() == Catch::Approx(600.0 / 700.0));

    // 障碍物完全在 used_x 之外时不影响分母
    load.placements = {{"a", "t", "", {0, 0, 0}, Orientation::XYZ, {3, 10, 10}}};
    load.used_volume = 300;
    REQUIRE(load.used_x() == 3);
    REQUIRE(load.usable_volume_x() == 300);
    REQUIRE(load.volume_rate_x() == Catch::Approx(1.0));
}

TEST_CASE("usable_volume_x: 斜面贯穿 X", "[core]")
{
    ContainerType ct;
    ct.inner_size = {10, 10, 10};
    ct.facets = {{0, -10, -10}}; // y+z>10 禁区（贯穿 X 全长）

    ContainerLoad load;
    load.type = &ct;
    load.placements = {{"a", "t", "", {0, 0, 0}, Orientation::XYZ, {5, 5, 5}}};
    load.used_volume = 125;

    REQUIRE(load.used_x() == 5);
    // slab = 5·10·10 = 500，楔形截面 (10·10/2) × used_x 5 = 250，可用 250
    REQUIRE(load.usable_volume_x() == 250);
    REQUIRE(load.volume_rate_x() == Catch::Approx(0.5));
}

TEST_CASE("usable_volume_x: 斜面非贯穿 X 按 X 向截断且与标准口径对齐", "[core]")
{
    ContainerType ct;
    ct.inner_size = {20, 10, 10};
    ct.facets = {{-10, 0, -10}}; // x+z>20 禁区（贯穿 Y）

    ContainerLoad load;
    load.type = &ct;
    load.placements = {{"a", "t", "", {0, 0, 0}, Orientation::XYZ, {15, 10, 5}}};
    load.used_volume = 750;

    REQUIRE(load.used_x() == 15);
    // slab = 15·10·10 = 1500，楔形 x∈[10,15] 段梯形体积 = 10·(0+5)/2·5 = 125，可用 1375
    REQUIRE(load.usable_volume_x() == 1375);
    REQUIRE(load.volume_rate_x() == Catch::Approx(750.0 / 1375.0));

    // used_x 达容器全长时，X 方向口径与标准口径一致（slab = 整个容器）
    load.placements = {{"a", "t", "", {0, 0, 0}, Orientation::XYZ, {20, 10, 5}}};
    load.used_volume = 1000;
    REQUIRE(load.used_x() == 20);
    REQUIRE(load.usable_volume_x() == load.usable_volume());
    REQUIRE(load.volume_rate_x() == Catch::Approx(1000.0 / 1500.0));
}
