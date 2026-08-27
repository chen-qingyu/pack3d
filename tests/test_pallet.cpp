#include "test_common.hpp"

// test_pallet_basic.json — 2 组散件全部装托 + 普通箱子直接装车
TEST_CASE("pallet 基本：全部装托 + 计数口径", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_basic.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    REQUIRE(res["summary"]["palletized_box_count"] == 8);
    REQUIRE(res["summary"]["loose_box_count"] == 4);
    REQUIRE(res["summary"]["packed_box_count"] == 12);
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    REQUIRE(res["result"]["pallets"].size() == 2);
    // 同组不拆托：每托恰一个组
    REQUIRE(res["result"]["pallets"][0]["groups"] == json::array({"A"}));
    REQUIRE(res["result"]["pallets"][1]["groups"] == json::array({"B"}));
    // 容器 packed_count 按原始散箱口径折算（5+3 装托 + 4 散装）
    REQUIRE(res["result"]["containers"][0]["packed_count"] == 12);
    // 托盘货物重 = 箱重合计（不含自重）
    REQUIRE(res["result"]["pallets"][0]["used_weight"] == 25.0);
    // 容器内托盘单元 box_id == pallet_id
    REQUIRE(res["result"]["containers"][0]["placements"][0]["box_id"] == "pt1200#1");
}

// test_pallet_resume.json — 装托 + 续装：已有 truck 已装 e1（reg 40³），新散件 l1 装托成
// pt#1，与 r1 一起续塞进已有容器；重量口径必须含已有放置（回归 prefill_load 丢重量）
TEST_CASE("pallet 续装：已有放置兼容 + 重量口径", "[solver][pallet][resume]")
{
    auto input = load_data("data/tests/test_pallet_resume.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["container_count"] == 1);
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 1);
    REQUIRE(res["summary"]["packed_box_count"] == 3); // e1 + pt#1(内 l1) + r1
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    const auto& containers = res["result"]["containers"];
    REQUIRE(containers.size() == 1);
    // 已有 e1 保留，托盘与 r1 进入同一容器
    std::set<std::string> ids;
    for (const auto& pl : containers[0]["placements"])
    {
        ids.insert(pl["box_id"].get<std::string>());
    }
    REQUIRE(ids.count("e1") == 1);
    REQUIRE(ids.count("pt#1") == 1);
    REQUIRE(ids.count("r1") == 1);
    // 重量口径：e1(20) + 托盘单元(10) + r1(20) = 50
    REQUIRE(containers[0]["used_weight"].get<double>() == Catch::Approx(50.0));
}

// test_pallet_oversize.json — 散件装不进任何托盘：默认 partial + violation；
// 改写 constraints.pallet_fallback=true → 降级散装 complete
TEST_CASE("pallet oversize/fallback 处理", "[solver][pallet]")
{
    const auto base = load_data("data/tests/test_pallet_oversize.json");

    // 默认（fallback=false）：partial + not palletized
    auto res = run(base);
    REQUIRE(res["status"] == "partial");
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 4);
    REQUIRE(res["summary"]["loose_box_count"] == 2);
    // 未装托散件计入未装箱
    REQUIRE(res["summary"]["unpacked_box_count"] == 1);
    REQUIRE(res["result"]["unpacked_boxes"] == json::array({"big1"}));
    bool found = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("not palletized") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);

    // pallet_fallback=true：未装托散件降级散装 → complete
    auto fb = base;
    fb["constraints"]["pallet_fallback"] = true;
    res = run(fb);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 1);
    REQUIRE(res["summary"]["palletized_box_count"] == 4);
    REQUIRE(res["summary"]["loose_box_count"] == 3); // 2 普通 + big1 降级
    REQUIRE(res["summary"]["packed_box_count"] == 7);
    REQUIRE(res["summary"]["unpacked_box_count"] == 0);
    REQUIRE(res["result"]["unpacked_boxes"].empty());
}

// test_pallet_weight.json — 每托不超托盘额定载重
TEST_CASE("pallet 承重不超 payload", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_weight.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 3); // 每托 1 箱（60kg > 50kg 余量）
    REQUIRE(res["summary"]["palletized_box_count"] == 3);
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["used_weight"].get<double>() <= 100.0);
    }
}

// test_pallet_height.json — 堆高不超 max_height，used_height 口径正确
TEST_CASE("pallet 堆高不超 max_height", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_height.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    REQUIRE(res["summary"]["palletized_box_count"] == 30);
    for (const auto& p : res["result"]["pallets"])
    {
        // 装载限高 max_height(450)（不含托盘自身高度）= 450
        REQUIRE(p["used_height"].get<int>() <= 450);
    }
    REQUIRE(res["result"]["pallets"][0]["used_height"] == 400); // 2 层 × 200
}

// test_pallet_group.json — 同组不拆托（软）：无混合托
TEST_CASE("pallet 同组不拆托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_group.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 3); // A(5) B(2) C(1) 各一托
    REQUIRE(res["summary"]["palletized_box_count"] == 8);
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["groups"].size() == 1); // 无混合托
    }

    // 当各 group 单独都无法整组装入时，允许混组可以减少托盘数
    auto constrained = input;
    constrained["box_types"][0]["sx"] = 600;
    constrained["box_types"][0]["sy"] = 500;
    constrained["box_types"][0]["sz"] = 1000;
    constrained["route"] = json::array({"P1"});
    for (auto& box : constrained["boxes"])
    {
        box["platform"] = "P1";
    }
    auto no_mix = run(constrained);
    REQUIRE(no_mix["status"] == "complete");
    REQUIRE(no_mix["summary"]["pallet_count"] == 4);
    for (const auto& p : no_mix["result"]["pallets"])
    {
        REQUIRE(p["groups"].size() == 1);
    }

    constrained["constraints"]["pallet_mix_group"] = true;
    auto mix = run(constrained);
    REQUIRE(mix["status"] == "complete");
    REQUIRE(mix["summary"]["pallet_count"] == 3);
    bool found_mixed = false;
    for (const auto& p : mix["result"]["pallets"])
    {
        found_mixed |= p["groups"].size() > 1;
        REQUIRE(p["platforms"] == json::array({"P1"}));
    }
    REQUIRE(found_mixed);
    const auto& container_groups = mix["result"]["containers"][0]["groups"];
    REQUIRE(std::find(container_groups.begin(), container_groups.end(), "A") !=
            container_groups.end());
    REQUIRE(std::find(container_groups.begin(), container_groups.end(), "B") !=
            container_groups.end());
    REQUIRE(std::find(container_groups.begin(), container_groups.end(), "C") !=
            container_groups.end());
    bool found_mixed_placement = false;
    for (const auto& placement : mix["result"]["containers"][0]["placements"])
    {
        if (placement["box_id"].get<std::string>().rfind("pt1200#", 0) == 0 &&
            placement["group"].is_null())
        {
            found_mixed_placement = true;
            REQUIRE_FALSE(placement.contains("groups"));
        }
    }
    REQUIRE(found_mixed_placement);

    for (const auto* algorithm : {"gep", "glc", "rgs", "bsg"})
    {
        constrained["algorithm"] = algorithm;
        auto multi_algo = run(constrained);
        INFO("algorithm=" << algorithm);
        REQUIRE(multi_algo["status"] == "complete");
        bool has_mixed_pallet = false;
        for (const auto& p : multi_algo["result"]["pallets"])
        {
            has_mixed_pallet |= p["groups"].size() > 1;
            REQUIRE(p["platforms"] == json::array({"P1"}));
        }
        REQUIRE(has_mixed_pallet);
    }
}

// test_pallet_route.json — route 启用时同平台单托，装车后满足路线约束
TEST_CASE("pallet route 同平台单托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_route.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);
    std::set<std::string> pallet_platforms;
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["platforms"].size() == 1);
        pallet_platforms.insert(p["platforms"][0].get<std::string>());
    }
    REQUIRE(pallet_platforms == std::set<std::string>({"P1", "P2"}));
    // 容器中托盘单元带平台（参与路线约束）
    for (const auto& pl : res["result"]["containers"][0]["placements"])
    {
        if (pl["box_id"].get<std::string>().rfind("pt1200#", 0) == 0)
        {
            REQUIRE(!pl["platform"].is_null());
        }
    }
}

// test_pallet_platform_split.json — 不同站点的货物不能混装一托（混合兜底也按站点分桶）
TEST_CASE("pallet 不同站点不混装一托", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_platform_split.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2); // P1/P2 各一托，不混装
    std::set<std::string> pallet_platforms;
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["platforms"].size() == 1);
        pallet_platforms.insert(p["platforms"][0].get<std::string>());
    }
    REQUIRE(pallet_platforms == std::set<std::string>({"P1", "P2"}));
}

// test_pallet_no_stack.json — 装车不叠托（单向）：托盘上无任何箱；托盘可架在普通箱子上方
TEST_CASE("pallet 装车不叠托（单向）", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_no_stack.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    const auto& placements = res["result"]["containers"][0]["placements"];
    const json* pallet_pl = nullptr;
    for (const auto& pl : placements)
    {
        if (pl["box_id"].get<std::string>().rfind("pt1200#", 0) == 0)
        {
            pallet_pl = &pl;
            break;
        }
    }
    REQUIRE(pallet_pl != nullptr);
    // 托盘架在普通箱子（地板 slab）上方
    REQUIRE((*pallet_pl)["z"] == 100);
    const int pallet_top = (*pallet_pl)["z"].get<int>() + (*pallet_pl)["dz"].get<int>();
    // 托盘顶面之上无任何箱子
    for (const auto& pl : placements)
    {
        if (&pl == pallet_pl)
        {
            continue;
        }
        REQUIRE(pl["z"].get<int>() != pallet_top);
    }
}

// test_pallet_invalid.json — 装托输入非法返回 invalid
TEST_CASE("pallet 非法输入返回 invalid", "[solver][pallet]")
{
    const auto base = load_data("data/tests/test_pallet_invalid.json");

    // max_height 非法（0，低于 schema 下限 1）
    auto h = base;
    h["pallet_types"][0]["max_height"] = 0;
    REQUIRE(run(h)["status"] == "invalid");

    // 装托模式强制全重量：箱子缺重量
    auto w = base;
    w["boxes"][0].erase("weight");
    REQUIRE(run(w)["status"] == "invalid");

    // 装托模式强制全重量：容器缺 payload
    auto cw = base;
    cw["container_types"][0].erase("payload");
    REQUIRE(run(cw)["status"] == "invalid");

    // loose 声明但无 pallet_types
    auto np = base;
    np.erase("pallet_types");
    REQUIRE(run(np)["status"] == "invalid");

    // 重复 pallet_type id
    auto dup = base;
    dup["pallet_types"].push_back(dup["pallet_types"][0]);
    REQUIRE(run(dup)["status"] == "invalid");

    // 装托模式必须显式配置是否混订单
    auto missing_mix = base;
    missing_mix["constraints"].erase("pallet_mix_group");
    REQUIRE(run(missing_mix)["status"] == "invalid");
}

// test_pallet_platform_restrict.json — 受限托盘只在对应平台装托；其余平台落到全平台托盘
TEST_CASE("pallet 平台限制：对应平台才用对应托盘", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_platform_restrict.json");
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    REQUIRE(res["summary"]["pallet_count"] == 2);

    // 按实际的承载平台记录托盘 type_id
    std::map<std::string, std::string> type_by_platform;
    for (const auto& p : res["result"]["pallets"])
    {
        REQUIRE(p["platforms"].size() == 1);
        type_by_platform[p["platforms"][0].get<std::string>()] = p["type_id"].get<std::string>();
    }
    // P1 命中 pt_p1（受限托盘只在 P1 可用）；P2 无匹配受限托盘，落到 pt_all
    REQUIRE(type_by_platform["P1"] == "pt_p1");
    REQUIRE(type_by_platform["P2"] == "pt_all");
}

// 平台受限导致某平台无可用托盘 → 默认 partial + not palletized；pallet_fallback=true 降级散装
TEST_CASE("pallet 平台限制：无可用托盘走兜底", "[solver][pallet]")
{
    auto base = load_data("data/tests/test_pallet_platform_restrict.json");
    auto input = base;
    // 把 pt_all 也限制到 P1，使 P2 无可用托盘
    input["pallet_types"][1]["platforms"] = json::array({"P1"});

    auto res = run(input);
    REQUIRE(res["status"] == "partial");
    bool has_violation = false;
    for (const auto& v : res["violations"])
    {
        if (v.get<std::string>().find("not palletized") != std::string::npos)
        {
            has_violation = true;
        }
    }
    REQUIRE(has_violation);
    // 仅 P1 装托成功
    REQUIRE(res["summary"]["pallet_count"] == 1);

    // pallet_fallback=true → 未装托散件降级散装上车 → complete
    input["constraints"]["pallet_fallback"] = true;
    auto fallback = run(input);
    REQUIRE(fallback["status"] == "complete");
    REQUIRE(fallback["summary"]["pallet_count"] == 1);
}

// 空串视为普通平台：限定 ["P1"] 时不用，限定 [""] 时可用作无平台货物
TEST_CASE("pallet 平台限制：空串作为普通平台", "[solver][pallet]")
{
    auto input = load_data("data/tests/test_pallet_platform_restrict.json");
    // 去掉所有箱子的 platform 与 route → 全部为无平台货物（内部 platform 为空串）
    for (auto& b : input["boxes"])
    {
        b.erase("platform");
    }
    input.erase("route");

    // 受限托盘限定在 ["P1"]：空串（无平台）货物不可用 → 只用 pt_all
    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    std::set<std::string> used;
    for (const auto& p : res["result"]["pallets"])
    {
        used.insert(p["type_id"].get<std::string>());
    }
    REQUIRE(used.count("pt_p1") == 0);
    REQUIRE(used.count("pt_all") == 1);

    // 受限托盘限定在 [""]：空串视为普通平台 → 可装载无平台货物
    input["pallet_types"][0]["platforms"] = json::array({""});
    auto res2 = run(input);
    REQUIRE(res2["status"] == "complete");
    std::set<std::string> used2;
    for (const auto& p : res2["result"]["pallets"])
    {
        used2.insert(p["type_id"].get<std::string>());
    }
    REQUIRE(used2.count("pt_p1") == 1);
}
