#include <catch2/catch_test_macros.hpp>

#include "core/algorithm/bsg/beam.hpp"
#include "core/algorithm/bsg/block.hpp"
#include "core/algorithm/bsg/greedy.hpp"
#include "core/algorithm/bsg/kpa.hpp"
#include "core/algorithm/bsg/solver.hpp"
#include "core/algorithm/bsg/types.hpp"
#include "core/tool.hpp"

using namespace pack3d;
using namespace pack3d::bsg;

static GlobalContext make_ctx_1box()
{
    GlobalContext ctx;
    ctx.container_size = {100, 100, 100};
    BoxType bt;
    bt.id = "box_s";
    bt.size = {50, 50, 50};
    bt.allowed_orientations = {Orientation::XYZ};
    ctx.box_types.push_back(bt);
    ctx.box_ids = {"b1"};
    return ctx;
}

TEST_CASE("KPA: dp and f(b,r)", "[bsg][kpa]")
{
    auto ctx = make_ctx_1box();
    BSGState s;
    s.remaining_counts = {1};
    s.R.push_back({{0, 0, 0}, 100, 100, 100});
    run_kpa(s, ctx);

    REQUIRE(s.kpa_L.has_value());
    REQUIRE(static_cast<int>(s.kpa_L->size()) == 101);
    CHECK((*s.kpa_L)[49] == 0);
    CHECK((*s.kpa_L)[50] == 50);
    CHECK((*s.kpa_L)[100] == 50);

    GeneralBlock b;
    b.osize = {50, 50, 50};
    b.members.push_back({0, 1});
    b.total_box_count = 1;
    b.single_box_volume = 125000;

    Cuboid r{{0, 0, 0}, 100, 100, 100};
    int64_t fv = compute_f(s, r, b, ctx);
    // V(b)=125000, V(r)=1e6, V_loss=875000, f=-750000
    CHECK(fv == -750000);
}

TEST_CASE("block generation: simple", "[bsg][block]")
{
    Size container{100, 100, 100};
    std::vector<BoxType> box_types;
    BoxType bt;
    bt.id = "box_s";
    bt.size = {50, 50, 50};
    bt.allowed_orientations = {Orientation::XYZ};
    box_types.push_back(bt);

    auto blocks = generate_blocks(container, box_types, {1}, 1.0, 10000);
    REQUIRE(blocks.size() >= 1);
    CHECK(blocks[0].is_simple());
    CHECK(blocks[0].nx == 1);
    CHECK(blocks[0].ny == 1);
    CHECK(blocks[0].nz == 1);
    CHECK(blocks[0].merge_axis == GeneralBlock::MergeAxis::None);
}

TEST_CASE("greedy rollout: 1 box", "[bsg][greedy]")
{
    auto ctx = make_ctx_1box();
    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, {1}, 1.0, 10000);
    ctx.blocks = std::move(blocks);

    BSGState s;
    s.remaining_counts = {1};
    s.R.push_back({{0, 0, 0}, 100, 100, 100});
    for (int i = 0; i < static_cast<int>(ctx.blocks.size()); ++i)
        s.available_blocks.push_back(i);

    auto gr = greedy_rollout(s, 0, ctx);
    CHECK(gr.total_volume == 125000);
    CHECK(gr.packed_counts[0] == 1);
    CHECK(gr.final_state.placements.size() == 1);
}

TEST_CASE("beam search: 1 box + double effort", "[bsg][beam]")
{
    TimeChecker::init(3600.0);
    auto ctx = make_ctx_1box();
    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, {1}, 1.0, 10000);
    ctx.blocks = std::move(blocks);

    BSGState s0;
    s0.remaining_counts = {1};
    s0.R.push_back({{0, 0, 0}, 100, 100, 100});
    for (int i = 0; i < static_cast<int>(ctx.blocks.size()); ++i)
        s0.available_blocks.push_back(i);

    // w=1
    int64_t vol = 0;
    BSGState best;
    int64_t r = beam_search(s0, 1, vol, best, ctx);
    CHECK(r >= 0);
    CHECK(vol == 125000);
    CHECK(best.placements.size() == 1);

    // w=2: s_best persists across rounds
    int64_t r2 = beam_search(s0, 2, vol, best, ctx);
    CHECK(r2 >= 0);
    CHECK(vol == 125000);
}

TEST_CASE("solver: 1 box", "[bsg][solver]")
{
    auto ctx = make_ctx_1box();
    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, {1}, 1.0, 10000);
    ctx.blocks = std::move(blocks);

    PackResult pr = solve(ctx, {1}, {{"b1"}}, 120.0);
    CHECK(pr.success);
    CHECK(pr.placements.size() == 1);
    CHECK(pr.used_volume == 125000);
    CHECK(pr.unpacked_box_ids.empty());
}

TEST_CASE("solver: 122 boxes (br00_001 scale)", "[bsg][solver]")
{
    GlobalContext ctx;
    ctx.container_size = {587, 233, 220};
    BoxType bt;
    bt.id = "t1";
    bt.size = {108, 76, 30};
    bt.allowed_orientations = {Orientation::XYZ, Orientation::YXZ};
    ctx.box_types.push_back(bt);

    std::vector<int> counts = {122};
    std::vector<std::vector<std::string>> ids(1);
    for (int i = 0; i < 122; ++i)
        ids[0].push_back("b" + std::to_string(i + 1));

    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, counts, 1.0, 10000);
    REQUIRE(blocks.size() == 203);
    ctx.blocks = std::move(blocks);

    PackResult pr = solve(ctx, counts, ids, 120.0);
    CHECK(pr.used_volume > 0);
    CHECK(pr.placements.size() > 0);
    CHECK(pr.placements.size() >= 100);
}
