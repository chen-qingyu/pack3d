// 输入预校验（pre_validate_input）规则单测：重复 ID、路线、承重、重量三选一、
// group 一致性、tender、装托模式等非法输入的检测与合法输入的放行
#include <catch2/catch_test_macros.hpp>

#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

TEST_CASE("pre_validate_input 检测重复 ID", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.container_types.push_back({"ct1", {2000, 2000, 2000}, 2000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});

    auto violations = pre_validate_input(p);
    bool found_dup = false;
    for (const auto& v : violations)
    {
        if (v.find("duplicate") != std::string::npos)
        {
            found_dup = true;
        }
    }
    REQUIRE(found_dup);
}

TEST_CASE("pre_validate_input 检测路线缺失平台", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", "Z"});

    RouteOrder route;
    route.platform_order = {"A", "B"};
    route.index_of["A"] = 0;
    route.index_of["B"] = 1;
    p.route = route;

    auto violations = pre_validate_input(p);
    bool found_route = false;
    for (const auto& v : violations)
    {
        if (v.find("not in route") != std::string::npos)
        {
            found_route = true;
        }
    }
    REQUIRE(found_route);
}

TEST_CASE("pre_validate_input 检测 max_stack 数组长度不匹配", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ, Orientation::YXZ};
    bt.max_stack = {2, 3, 4}; // 长度 3 != 朝向数 2
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("max_stack array length") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input 检测 max_load 需要重量", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.max_load = {50.0};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""}); // 无重量

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("max_load requires weight info (box_types or boxes)") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input 检测 max_stack/max_load 需要 support_rate", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.max_stack = {2};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});
    p.support_rate = 0.0; // 默认即 0，显式写出

    // support_rate=0 + max_stack → 报错
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("max_stack/max_load requires support_rate > 0") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // support_rate>0 → 不再报错
    p.support_rate = 1.0;
    violations = pre_validate_input(p);
    for (const auto& v : violations)
    {
        REQUIRE(v.find("max_stack/max_load requires support_rate > 0") == std::string::npos);
    }
}

TEST_CASE("pre_validate_input 检测 heavy_not_on_light 需要重量", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""}); // 无重量
    p.heavy_not_on_light = true;
    p.support_rate = 1.0;

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("heavy_not_on_light requires weight info") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input 检测 heavy_not_on_light 需要 support_rate", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});
    p.heavy_not_on_light = true;
    p.support_rate = 0.0;

    // support_rate=0 + heavy_not_on_light → 报错
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("heavy_not_on_light requires support_rate > 0") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // support_rate>0 且带重量 → 不再报错
    p.support_rate = 1.0;
    violations = pre_validate_input(p);
    for (const auto& v : violations)
    {
        REQUIRE(v.find("heavy_not_on_light requires") == std::string::npos);
    }
}

// 箱型级重量：箱型与箱子不能同时有重量
TEST_CASE("pre_validate_input 重量三选一：箱型与箱子重量互斥", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.weight = 10.0;
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "", ""}); // 箱子也带重量 → 互斥

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent weight") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 箱型级重量：要么全部箱型有重量，要么全都没有
TEST_CASE("pre_validate_input 重量三选一：箱型重量必须全部配置", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt1;
    bt1.id = "bt1";
    bt1.size = {100, 100, 100};
    bt1.allowed_orientations = {Orientation::XYZ};
    bt1.weight = 10.0;
    BoxType bt2;
    bt2.id = "bt2";
    bt2.size = {50, 50, 50};
    bt2.allowed_orientations = {Orientation::XYZ}; // 无重量
    p.box_types.push_back(bt1);
    p.box_types.push_back(bt2);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});
    p.boxes.push_back({"box2", "bt2", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent weight") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 箱型级重量模式（全箱型有、箱子无）合法
TEST_CASE("pre_validate_input 箱型级重量模式通过", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.weight = 10.0;
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    REQUIRE(violations.empty());
}

// 箱子级模式下 existing placement 必须带 group
TEST_CASE("pre_validate_input group 一致性：已有放置缺 group 非法", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "g1", ""});
    ExistingPlacement ep;
    ep.box_id = "e1";
    ep.box_type_id = "bt1";
    ep.position = {0, 0, 0};
    ep.orientation = Orientation::XYZ;
    p.existing_containers.push_back({"ct1", {ep}}); // 已有放置缺 group

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        found |= v.find("inconsistent group") != std::string::npos;
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input existing 快照遵守三选一来源", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 5.0, "", ""});
    ExistingPlacement ep;
    ep.box_id = "e1";
    ep.box_type_id = "bt1";
    ep.position = {0, 0, 0};
    ep.orientation = Orientation::XYZ;
    ep.weight = 5.0;
    ep.group = "g1";
    p.existing_containers.push_back({"ct1", {ep}});

    auto violations = pre_validate_input(p);
    bool found_group = false;
    for (const auto& v : violations)
    {
        found_group |= v.find("inconsistent group") != std::string::npos;
    }
    REQUIRE(found_group);

    p.boxes[0].group = "g1";
    REQUIRE(pre_validate_input(p).empty());
}

TEST_CASE("pre_validate_input group/platform 箱型级模式并归一化", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.group = "g1";
    bt.platform = "P1";
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});
    ExistingPlacement ep;
    ep.box_id = "existing1";
    ep.box_type_id = "bt1";
    ep.position = {0, 0, 0};
    ep.orientation = Orientation::XYZ;
    p.existing_containers.push_back({"ct1", {ep}});
    p.route = RouteOrder{{"P1"}, {{"P1", 0}}};

    const auto source = p;
    REQUIRE(pre_validate_input(source).empty());
    resolve_type_fields(p);
    REQUIRE(p.boxes[0].group == "g1");
    REQUIRE(p.boxes[0].platform == "P1");
    REQUIRE(p.existing_containers[0].placements[0].group == "g1");
    REQUIRE(p.existing_containers[0].placements[0].platform == "P1");

    auto same = source;
    same.existing_containers[0].placements[0].group = "g1";
    same.existing_containers[0].placements[0].platform = "P1";
    same.box_types[0].weight = 10.0;
    same.existing_containers[0].placements[0].weight = 10.0;
    REQUIRE(pre_validate_input(same).empty());

    auto conflict = same;
    conflict.existing_containers[0].placements[0].group = "g2";
    conflict.existing_containers[0].placements[0].platform = "P2";
    const auto label_violations = pre_validate_input(conflict);
    bool found_group_conflict = false;
    bool found_platform_conflict = false;
    for (const auto& v : label_violations)
    {
        found_group_conflict |= v.find("inconsistent group") != std::string::npos;
        found_platform_conflict |= v.find("inconsistent platform") != std::string::npos;
    }
    REQUIRE(found_group_conflict);
    REQUIRE(found_platform_conflict);

    auto weight_conflict = source;
    weight_conflict.box_types[0].weight = 10.0;
    weight_conflict.existing_containers[0].placements[0].weight = 20.0;
    bool found_weight_conflict = false;
    for (const auto& v : pre_validate_input(weight_conflict))
    {
        found_weight_conflict |= v.find("inconsistent weight") != std::string::npos;
    }
    REQUIRE(found_weight_conflict);

    auto none = source;
    none.box_types[0].group.reset();
    none.box_types[0].platform.reset();
    none.boxes[0].group.clear();
    none.boxes[0].platform.clear();
    none.boxes[0].weight.reset();
    none.existing_containers[0].placements[0].group = "g1";
    none.existing_containers[0].placements[0].platform = "P1";
    none.existing_containers[0].placements[0].weight = 10.0;
    none.route = RouteOrder{{"P1"}, {{"P1", 0}}};
    const auto none_violations = pre_validate_input(none);
    bool found_none_group = false;
    bool found_none_platform = false;
    bool found_none_weight = false;
    for (const auto& v : none_violations)
    {
        found_none_group |= v.find("inconsistent group") != std::string::npos;
        found_none_platform |= v.find("inconsistent platform") != std::string::npos;
        found_none_weight |= v.find("inconsistent weight") != std::string::npos;
    }
    REQUIRE(found_none_group);
    REQUIRE(found_none_platform);
    REQUIRE(found_none_weight);
}

TEST_CASE("pre_validate_input group/platform 箱子级模式允许实例不同", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", std::nullopt, "g1", "P1"});
    p.boxes.push_back({"box2", "bt1", std::nullopt, "g2", "P2"});
    p.route = RouteOrder{{"P1", "P2"}, {{"P1", 0}, {"P2", 1}}};

    REQUIRE(pre_validate_input(p).empty());
}

TEST_CASE("pre_validate_input group/platform 禁止箱型与箱子混用", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.group = "g1";
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "g2", ""});

    const auto violations = pre_validate_input(p);
    bool found_group = false;
    for (const auto& v : violations)
    {
        found_group |= v.find("inconsistent group") != std::string::npos;
    }
    REQUIRE(found_group);
}

TEST_CASE("pre_validate_input platform 箱子级模式禁止部分缺失", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", "P1"});
    p.boxes.push_back({"box2", "bt1", std::nullopt, "", ""});
    p.route = RouteOrder{{"P1"}, {{"P1", 0}}};

    const auto violations = pre_validate_input(p);
    bool found_platform = false;
    for (const auto& v : violations)
    {
        found_platform |= v.find("inconsistent platform") != std::string::npos;
    }
    REQUIRE(found_platform);
}

TEST_CASE("pre_validate_input 检测 tender_limit 需要 group", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""}); // 无 group
    p.tender_limit = 2;

    // tender_limit 但无 group → 报错（否则约束静默失效）
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("tender_limit requires group") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // 有 group → 不再报错
    p.boxes[0].group = "A";
    violations = pre_validate_input(p);
    for (const auto& v : violations)
    {
        REQUIRE(v.find("tender_limit requires group") == std::string::npos);
    }
}

TEST_CASE("pre_validate_input 检测装托模式需要 support_rate", "[validation]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});
    p.pallet_types.push_back({"pt", {100, 100, 10}, 100.0, 100, 0.0});
    p.support_rate = 0.0; // 默认即 0

    // 装托 + support_rate=0 → 报错（托盘可在车厢内悬空）
    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("pallet mode requires support_rate > 0") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // support_rate>0 → 不再报错
    p.support_rate = 0.6;
    violations = pre_validate_input(p);
    for (const auto& v : violations)
    {
        REQUIRE(v.find("pallet mode requires support_rate > 0") == std::string::npos);
    }
}
