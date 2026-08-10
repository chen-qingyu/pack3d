#include <catch2/catch_test_macros.hpp>

#include "core/algorithm/bsg/beam.hpp"
#include "core/algorithm/bsg/block.hpp"
#include "core/algorithm/bsg/expand.hpp"
#include "core/algorithm/bsg/greedy.hpp"
#include "core/algorithm/bsg/kpa.hpp"
#include "core/algorithm/bsg/solver.hpp"
#include "core/algorithm/bsg/space.hpp"
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
    // V(b)=125000, V_loss=875000, f=-750000.
    CHECK(fv == -750000);

    GeneralBlock sparse = b;
    sparse.osize = {100, 50, 50};
    sparse.single_box_volume = 125000;
    // The score must use packed box volume, not sparse block bounding volume.
    CHECK(compute_f(s, r, sparse, ctx) == -625000);
}

TEST_CASE("KPA: considers all allowed dimensions", "[bsg][kpa]")
{
    GlobalContext ctx;
    ctx.container_size = {80, 80, 80};

    BoxType box_type;
    box_type.id = "rotatable";
    box_type.size = {100, 50, 20};
    box_type.allowed_orientations = {Orientation::XYZ, Orientation::YXZ};
    ctx.box_types = {box_type};

    BSGState state;
    state.remaining_counts = {1};
    run_kpa(state, ctx);

    REQUIRE(state.kpa_L.has_value());
    CHECK((*state.kpa_L)[80] == 50);
}

TEST_CASE("space selection: all corners and volume tie-break", "[bsg][space]")
{
    Size container{10, 10, 10};
    std::vector<Cuboid> spaces{
        {{2, 0, 0}, 1, 1, 1},
        {{0, 2, 0}, 3, 3, 3},
    };

    SpaceSelection selection = select_free_space(spaces, container);
    CHECK(selection.cuboid_index == 1);
    CHECK(selection.anchor == Position{0, 2, 0});

    Cuboid cuboid{{2, 3, 4}, 5, 5, 5};
    selection = select_free_space({cuboid}, container);
    CHECK(selection.anchor == Position{2, 8, 9});

    GeneralBlock block;
    block.osize = {1, 1, 1};
    CHECK(placement_position(cuboid, block, selection.anchor) == Position{2, 7, 8});
}

TEST_CASE("residual space: uses overlapping cover", "[bsg][space]")
{
    std::vector<Cuboid> spaces{{{0, 0, 0}, 100, 100, 100}};
    update_residual_space(spaces, {0, 0, 0}, {50, 50, 50});

    REQUIRE(spaces.size() == 3);
    CHECK(spaces[0].pos == Position{50, 0, 0});
    CHECK(spaces[0].lx == 50);
    CHECK(spaces[0].ly == 100);
    CHECK(spaces[0].lz == 100);
    CHECK(spaces[1].pos == Position{0, 50, 0});
    CHECK(spaces[1].lx == 100);
    CHECK(spaces[1].ly == 50);
    CHECK(spaces[1].lz == 100);
    CHECK(spaces[2].pos == Position{0, 0, 50});
    CHECK(spaces[2].lx == 100);
    CHECK(spaces[2].ly == 100);
    CHECK(spaces[2].lz == 50);
    CHECK(spaces[0].overlaps(spaces[1]));
    CHECK(spaces[0].overlaps(spaces[2]));
    CHECK(spaces[1].overlaps(spaces[2]));
}

TEST_CASE("expand: enforces max_stack constraint", "[bsg][support]")
{
    GlobalContext ctx;
    ctx.container_size = {100, 100, 100};
    ctx.container_type.id = "truck";
    ctx.container_type.inner_size = {100, 100, 100};
    ctx.support_rate = 1.0;
    ctx.has_max_stack = true; // 走逐叶校验路径

    BoxType box_type;
    box_type.id = "base";
    box_type.size = {100, 100, 50};
    box_type.allowed_orientations = {Orientation::XYZ};
    box_type.max_stack = {1}; // 不可堆叠
    ctx.box_types = {box_type};
    ctx.box_type_map = {{"base", box_type}};
    ctx.item_classes = {{"base", "", 0.0, {"b1"}}};
    ctx.blocks = generate_blocks(ctx.container_size, ctx.box_types, {2}, 1.0, 10000);

    int block_index = -1;
    for (size_t i = 0; i < ctx.blocks.size(); ++i)
    {
        if (ctx.blocks[i].osize.dx == 100 &&
            ctx.blocks[i].osize.dy == 100 &&
            ctx.blocks[i].osize.dz == 50)
        {
            block_index = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(block_index >= 0);

    BSGState state;
    state.R = {{{0, 0, 50}, 100, 100, 50}};
    state.remaining_counts = {1};
    state.available_blocks = {block_index};
    state.placements = {{block_index, {0, 0, 0}}};
    state.constraint_load.type = &ctx.container_type;
    state.constraint_load.placements.push_back(
        {"", "base", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 50}});
    run_kpa(state, ctx);

    CHECK(expand(state, 1, ctx).empty());

    ctx.box_types[0].max_stack[0] = 3;
    ctx.box_type_map.at("base").max_stack[0] = 3;
    CHECK(expand(state, 1, ctx).size() == 1);
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

TEST_CASE("block generation: general blocks allow bounded gaps", "[bsg][block]")
{
    Size container{30, 20, 10};
    std::vector<BoxType> box_types;

    BoxType wide;
    wide.id = "wide";
    wide.size = {10, 20, 10};
    wide.allowed_orientations = {Orientation::XYZ};
    box_types.push_back(wide);

    BoxType narrow;
    narrow.id = "narrow";
    narrow.size = {10, 19, 10};
    narrow.allowed_orientations = {Orientation::XYZ};
    box_types.push_back(narrow);

    auto blocks = generate_blocks(container, box_types, {1, 1}, 0.95, 10000);
    bool found_general = false;
    for (const auto& block : blocks)
    {
        if (block.merge_axis == GeneralBlock::MergeAxis::X &&
            block.osize.dx == 20 && block.osize.dy == 20 && block.osize.dz == 10 &&
            block.members.size() == 2 && block.single_box_volume == 3900 &&
            block.volume() == 4000)
        {
            found_general = true;
            break;
        }
    }
    CHECK(found_general);
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
    TimeChecker::init(120.0);
    auto ctx = make_ctx_1box();
    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, {1}, 1.0, 10000);
    ctx.blocks = std::move(blocks);

    PackResult pr = solve(ctx, {1}, {{"b1"}});
    CHECK(pr.success);
    CHECK(pr.placements.size() == 1);
    CHECK(pr.used_volume == 125000);
    CHECK(pr.unpacked_box_ids.empty());
}

TEST_CASE("solver respects caller time limit", "[bsg][solver]")
{
    TimeChecker::init(0.0);
    auto ctx = make_ctx_1box();
    auto blocks = generate_blocks(ctx.container_size, ctx.box_types, {1}, 1.0, 10000);
    ctx.blocks = std::move(blocks);

    PackResult pr = solve(ctx, {1}, {{"b1"}});
    CHECK(!pr.success);
    CHECK(pr.placements.empty());
}
