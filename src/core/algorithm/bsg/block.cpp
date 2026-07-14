#include "block.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <unordered_set>

namespace pack3d::bsg
{

namespace
{

int64_t next_block_id() noexcept
{
    static int64_t id = 0;
    return id++;
}

// FNV-1a 64-bit hash combiner
uint64_t hash_combine(uint64_t seed, uint64_t val) noexcept
{
    constexpr uint64_t prime = 0x100000001b3ULL;
    return (seed ^ val) * prime;
}

uint64_t block_hash(const GeneralBlock& b) noexcept
{
    uint64_t h = 0;
    h = hash_combine(h, static_cast<uint64_t>(b.osize.dx));
    h = hash_combine(h, static_cast<uint64_t>(b.osize.dy));
    h = hash_combine(h, static_cast<uint64_t>(b.osize.dz));
    for (const auto& m : b.members)
    {
        h = hash_combine(h, static_cast<uint64_t>(m.type_idx));
        h = hash_combine(h, static_cast<uint64_t>(m.count));
    }
    return h;
}

GeneralBlock make_simple_block(
    int type_idx,
    Orientation orient,
    int nx, int ny, int nz,
    const OrientedSize& single)
{
    GeneralBlock b;
    b.id = next_block_id();
    b.osize.dx = single.dx * nx;
    b.osize.dy = single.dy * ny;
    b.osize.dz = single.dz * nz;
    b.members.push_back({type_idx, nx * ny * nz});
    b.total_box_count = b.members.back().count;
    b.single_box_volume = static_cast<int64_t>(single.dx) * single.dy * single.dz * b.total_box_count;
    b.merge_axis = GeneralBlock::MergeAxis::None;
    b.nx = nx;
    b.ny = ny;
    b.nz = nz;
    b.orientation = orient;
    b.type_idx = type_idx;
    return b;
}

std::vector<GeneralBlock::Member> merge_members(
    const std::vector<GeneralBlock::Member>& a,
    const std::vector<GeneralBlock::Member>& b)
{
    // a and b are each sorted by type_idx
    std::vector<GeneralBlock::Member> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (a[i].type_idx < b[j].type_idx)
        {
            result.push_back(a[i]);
            ++i;
        }
        else if (a[i].type_idx > b[j].type_idx)
        {
            result.push_back(b[j]);
            ++j;
        }
        else
        {
            result.push_back({a[i].type_idx, a[i].count + b[j].count});
            ++i;
            ++j;
        }
    }
    while (i < a.size())
    {
        result.push_back(a[i]);
        ++i;
    }
    while (j < b.size())
    {
        result.push_back(b[j]);
        ++j;
    }
    return result;
}

bool members_exceed_available(
    const std::vector<GeneralBlock::Member>& members,
    const std::vector<int>& available_counts)
{
    for (const auto& m : members)
    {
        if (m.type_idx < 0 || static_cast<size_t>(m.type_idx) >= available_counts.size())
        {
            return true;
        }
        if (m.count > available_counts[m.type_idx])
        {
            return true;
        }
    }
    return false;
}

// Try merging a and b along axis X. Returns the merged block if valid, nullopt otherwise.
std::optional<GeneralBlock> try_merge_x(
    const GeneralBlock& a, const GeneralBlock& b,
    const Size& container_size, double max_fr,
    const std::vector<int>& available_counts)
{
    int32_t new_dx = a.osize.dx + b.osize.dx;
    int32_t new_dy = std::max(a.osize.dy, b.osize.dy);
    int32_t new_dz = std::max(a.osize.dz, b.osize.dz);
    if (new_dx > container_size.x)
    {
        return std::nullopt;
    }
    int64_t new_vol = static_cast<int64_t>(new_dx) * new_dy * new_dz;
    int64_t box_vol = a.single_box_volume + b.single_box_volume;
    if (static_cast<double>(box_vol) / static_cast<double>(new_vol) < max_fr)
    {
        return std::nullopt;
    }
    auto members = merge_members(a.members, b.members);
    if (members_exceed_available(members, available_counts))
    {
        return std::nullopt;
    }
    GeneralBlock result;
    result.id = next_block_id();
    result.osize = {new_dx, new_dy, new_dz};
    result.members = std::move(members);
    result.total_box_count = a.total_box_count + b.total_box_count;
    result.single_box_volume = box_vol;
    result.merge_axis = GeneralBlock::MergeAxis::X;
    result.source_left_id = a.id;
    result.source_right_id = b.id;
    return result;
}

std::optional<GeneralBlock> try_merge_y(
    const GeneralBlock& a, const GeneralBlock& b,
    const Size& container_size, double max_fr,
    const std::vector<int>& available_counts)
{
    int32_t new_dx = std::max(a.osize.dx, b.osize.dx);
    int32_t new_dy = a.osize.dy + b.osize.dy;
    int32_t new_dz = std::max(a.osize.dz, b.osize.dz);
    if (new_dy > container_size.y)
    {
        return std::nullopt;
    }
    int64_t new_vol = static_cast<int64_t>(new_dx) * new_dy * new_dz;
    int64_t box_vol = a.single_box_volume + b.single_box_volume;
    if (static_cast<double>(box_vol) / static_cast<double>(new_vol) < max_fr)
    {
        return std::nullopt;
    }
    auto members = merge_members(a.members, b.members);
    if (members_exceed_available(members, available_counts))
    {
        return std::nullopt;
    }
    GeneralBlock result;
    result.id = next_block_id();
    result.osize = {new_dx, new_dy, new_dz};
    result.members = std::move(members);
    result.total_box_count = a.total_box_count + b.total_box_count;
    result.single_box_volume = box_vol;
    result.merge_axis = GeneralBlock::MergeAxis::Y;
    result.source_left_id = a.id;
    result.source_right_id = b.id;
    return result;
}

std::optional<GeneralBlock> try_merge_z(
    const GeneralBlock& a, const GeneralBlock& b,
    const Size& container_size, double max_fr,
    const std::vector<int>& available_counts)
{
    int32_t new_dx = std::max(a.osize.dx, b.osize.dx);
    int32_t new_dy = std::max(a.osize.dy, b.osize.dy);
    int32_t new_dz = a.osize.dz + b.osize.dz;
    if (new_dz > container_size.z)
    {
        return std::nullopt;
    }
    int64_t new_vol = static_cast<int64_t>(new_dx) * new_dy * new_dz;
    int64_t box_vol = a.single_box_volume + b.single_box_volume;
    if (static_cast<double>(box_vol) / static_cast<double>(new_vol) < max_fr)
    {
        return std::nullopt;
    }
    auto members = merge_members(a.members, b.members);
    if (members_exceed_available(members, available_counts))
    {
        return std::nullopt;
    }
    GeneralBlock result;
    result.id = next_block_id();
    result.osize = {new_dx, new_dy, new_dz};
    result.members = std::move(members);
    result.total_box_count = a.total_box_count + b.total_box_count;
    result.single_box_volume = box_vol;
    result.merge_axis = GeneralBlock::MergeAxis::Z;
    result.source_left_id = a.id;
    result.source_right_id = b.id;
    return result;
}

} // namespace

std::vector<GeneralBlock> generate_blocks(
    const Size& container_size,
    const std::vector<BoxType>& box_types,
    const std::vector<int>& available_counts,
    double max_fr,
    int max_bl)
{
    std::vector<GeneralBlock> blocks;
    std::unordered_set<uint64_t> seen_hashes;

    auto add_block = [&](GeneralBlock& b) -> bool
    {
        // 维度超容器：丢弃
        if (b.osize.dx > container_size.x ||
            b.osize.dy > container_size.y ||
            b.osize.dz > container_size.z)
        {
            return false;
        }
        uint64_t h = block_hash(b);
        if (seen_hashes.count(h))
        {
            return false;
        }
        seen_hashes.insert(h);
        blocks.push_back(std::move(b));
        return true;
    };

    // ---- 阶段 1：simple blocks ----
    for (size_t ti = 0; ti < box_types.size(); ++ti)
    {
        int available = available_counts[ti];
        if (available <= 0)
        {
            continue;
        }

        const auto& bt = box_types[ti];
        for (auto orient : bt.allowed_orientations)
        {
            auto single = bt.size.orient(orient);
            if (single.dx > container_size.x ||
                single.dy > container_size.y ||
                single.dz > container_size.z)
            {
                continue;
            }

            int max_nx = container_size.x / single.dx;
            int max_ny = container_size.y / single.dy;
            int max_nz = container_size.z / single.dz;

            for (int nx = 1; nx <= max_nx; ++nx)
            {
                for (int ny = 1; ny <= max_ny; ++ny)
                {
                    for (int nz = 1; nz <= max_nz; ++nz)
                    {
                        int count = nx * ny * nz;
                        if (count > available)
                        {
                            continue;
                        }

                        auto b = make_simple_block(static_cast<int>(ti), orient, nx, ny, nz, single);
                        add_block(b);
                        if (static_cast<int>(blocks.size()) >= max_bl)
                        {
                            goto done;
                        }
                    }
                }
            }
        }
    }

    // ---- 阶段 2：增量合并 ----
    {
        size_t round_start = 0; // start of "new" blocks for current merge round

        while (true)
        {
            size_t prev_total = blocks.size();
            if (prev_total >= static_cast<size_t>(max_bl))
            {
                break;
            }

            // merge new blocks (from round_start onwards) with ALL existing blocks
            for (size_t i = round_start; i < prev_total; ++i)
            {
                for (size_t j = 0; j < prev_total; ++j)
                {
                    if (i == j)
                    {
                        continue;
                    }

                    const auto& a = blocks[i];
                    const auto& b = blocks[j];

                    auto mx = try_merge_x(a, b, container_size, max_fr, available_counts);
                    auto my = try_merge_y(a, b, container_size, max_fr, available_counts);
                    auto mz = try_merge_z(a, b, container_size, max_fr, available_counts);

                    if (mx.has_value())
                    {
                        add_block(*mx);
                        if (static_cast<int>(blocks.size()) >= max_bl)
                        {
                            goto done;
                        }
                    }

                    if (my.has_value())
                    {
                        add_block(*my);
                        if (static_cast<int>(blocks.size()) >= max_bl)
                        {
                            goto done;
                        }
                    }

                    if (mz.has_value())
                    {
                        add_block(*mz);
                        if (static_cast<int>(blocks.size()) >= max_bl)
                        {
                            goto done;
                        }
                    }
                }
            }

            if (blocks.size() == prev_total)
            {
                break;
            } // no new blocks this round
            round_start = prev_total;
        }
    }

done:
    // 按体积降序排列（方便后续候选选取）
    std::sort(blocks.begin(), blocks.end(),
              [](const GeneralBlock& a, const GeneralBlock& b) noexcept
              {
                  return a.volume() > b.volume();
              });
    return blocks;
}

} // namespace pack3d::bsg
