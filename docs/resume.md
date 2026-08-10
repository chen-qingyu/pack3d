# 中间状态续装（Resume Packing）

从已有部分放置继续装箱——输入通过 `existing_containers` 描述已放置的容器和箱子，求解器在此基础上继续放置剩余箱子。

---

## 设计原理

### 核心约束

- **已有放置不可移动**：`ContainerLoad::locked = true`，后处理（`reduce_platform_splits`、`repack_last_smaller`）跳过 locked 容器。
- **剩余箱子独立输入**：`boxes` 列表只列待装箱子，已放置箱子完全由 `existing_containers` 描述，两不相交。
- **输入输出格式对齐**：`existing_containers` 的 placement 字段与输出 placement 完全一致（`box_id`、`box_type_id`、`x/y/z`、`dx/dy/dz`、`orientation`、`weight`、`platform`、`group`），上轮输出可直接 copy-paste 为下轮输入。

### 三阶段主循环

```
pack()
  │
  ├─ 阶段 A: 预填充已有容器
  │   build_load_from_existing() → all_loads（locked=true）
  │
  ├─ 阶段 B: 继续塞已有容器
  │   for each cl in all_loads:
  │     extra = pack_single(remaining, ct, cl.placements)
  │     用 extra 覆盖 cl 的可变字段
  │
  └─ 阶段 C: 开新容器
      pack_single(remaining, ct, {})  // existing 为空
```

阶段 B 将已有 `placements` 作为 `existing` 参数传入 `pack_single`，每个算法基于已有空间占用搜索新箱子的合法位置。新增箱子合并回原容器后，累计值（volume、weight、platforms、groups）直接用 `extra` 覆盖，避免逐 placement 重复累加。

---

## 预校验

`pre_validate_input()` 对已有容器做完整校验：

| 校验项         | 说明                                                               |
| -------------- | ------------------------------------------------------------------ |
| 容器类型存在性 | `type_id` 必须在 `container_types` 中                              |
| 箱子类型存在性 | `box_type_id` 必须在 `box_types` 中                                |
| 容器数量限制   | `quantity_limit` 超限检测                                          |
| 重复 box_id    | 同一容器内、跨容器均不可重复                                       |
| 边界           | `check_boundary`                                                   |
| 重叠           | `check_overlap`（仅与同容器已校验放置）                            |
| 支撑率         | `check_support`                                                    |
| 站点限制       | `check_platform_limit`                                             |
| 路线顺序       | `check_route_order`                                                |
| 重量上限       | `total_weight` vs `max_weight`                                     |
| size 一致性    | 若提供 `dx`/`dy`/`dz`，必须与 `box_type_id`+`orientation` 推导一致 |

---

## 分算法实现

### GEP（极点贪心）

**原理**：GEP 维护一个极点（Extreme Point）集合，每次选择体积最大的未放置箱子，在极点中找第一个合法位置放置，放置后生成三个新极点。

**已有放置处理**：

1. 将 `existing` 中的 placement 预填入 `load`（volume、weight、platforms、groups 全部同步）。
2. 从每个已有放置的顶面生成三个初始极点：
   ```
   (x+dx, y, z), (x, y+dy, z), (x, y, z+dz)
   ```
3. 原点 `(0,0,0)` 也加入极点集（兜底）。

**特点**：极点是增量计算的，已有放置的极点直接注入初始集合即可无缝衔接后续搜索。实现最简单。

---

### GLC（块装载 + Beam 搜索）

**原理**：GLC 将同类型箱子合并为简单块（`SimpleBlock`，nx×ny×nz 网格），在空间栈（Space Stack）上贪心放置块，通过 Beam 搜索（宽度 6~16）保留多路方案。空间管理使用三向切割（Z/X/Y），碎片通过 `transfer_space` 合并回收。

**已有放置处理——Space 栈重建**：

GLC 的核心难点：已有放置可能不在任何 Space 的角落位置，无法直接用 `split_space` 切割。

`reconstruct_spaces()` 分两路处理：

**路径 1 —— 角落匹配（快路径）**：
将已有放置按 (z, y, x) 升序排列（接近 GLC 自然放置顺序），对每个放置从栈顶向下找第一个位置和尺寸均匹配的 Space，命中则走标准 `split_space`。

**路径 2 —— 6-slab 完整分解（兜底）**：
若放置不在任何 Space 角落，则用 `carve_out_space()` 从包含它的 Space 中挖掉该区域。6-slab 完整分解 `space − box`（左/右/前/后/下/上，完整覆盖全部剩余空间，不丢对角空间），小块在栈顶优先处理。

**已有放置预填充**：

- 将 `existing` placements 推入 `state.placements`，累计 volume、weight、platforms、groups。
- 若 `existing` 非空，用 `reconstruct_spaces` 替代初始单空间 `(0,0,0, lx, ly, lz)`。

---

### RGS（随机贪心搜索）

**原理**：RGS 在多轮迭代中，对箱子进行随机排序 + 极点贪心放置，通过多起点（5 种排序策略 × 多次随机）搜索最优方案。

**已有放置处理**：

共享的 `prefill_load()` 预填充 `ContainerLoad`（placements、volume、weight、platforms、groups），`prefill_ep()` 预填充 `EpContext`（从每个已有放置的三面生成极点，外加原点 `(0,0,0)`）。

每轮迭代中，`insertion_heuristic` 在已有极点和已有放置的基础上增量搜索。与 GEP 类似，极点机制的增量特性使续装实现简单直接。

**注意**：RGS 的 `best_load` 在多个迭代间比较，已有放置的累计值在每个迭代中独立预填充，`best_load` 最终包含已有+新增的完整状态。

---

### BSG（束搜索）

**原理**：BSG 将箱子预合并为复合块（`GeneralBlock`），通过递增 beam width 的束搜索在残差空间（Residual Space）上放置块。约束模式下走逐箱叶子验证（`feasibility.cpp`），非约束模式走块级支撑检查。

**已有放置处理——双层机制**：

**Solver 层**（`bsg::solve`）：

- 从 `ctx.existing_placements` 读取已有放置。
- 更新残差空间：`update_residual_space(s0.R, pl.position, pl.osize)` ——已有放置从残差空间中挖掉。
- 累计 `s0.used_volume`（含已有体积，影响 beam 排名）。
- 约束模式下：将已有放置推入 `s0.constraint_load`（供 `can_place_block` 逐箱校验）。
- 约束模式下：同步 `constraint_load.used_volume`。

`pr.used_volume` 在两种模式下均已包含已有体积（solver 内部 `s0.used_volume` 已预填），`pack_single` 直接使用。

**Packer 层**（`BsgPacker::pack_single`）：

- 先将 `existing` 中的 placement 预填入返回的 `ContainerLoad`（与其他三算法一致）。
- 再将 solver 返回的 `pr.placements` 追加其后。
- 回填循环仅遍历新增的 placement（从 `existing.size()` 开始），通过 `box_map_` 补充 `platform`、`group`、`weight` 元数据。

**特点**：solver 内部不感知 placement 级别的已有箱子（仅处理残差空间），最终输出的 placement 合并由 packer 层完成。这使得 solver 逻辑修改最小化。

---

## 后处理兼容

两个后处理步骤均跳过 locked 容器：

- **`reduce_platform_splits`**：遍历容器时 `if (all_loads[ci].locked) continue;`，不会尝试从已有容器中移动站点。
- **`repack_last_smaller`**：`if (cl.placements.empty() || cl.locked) return;`，不会尝试将已有容器换到更小的车型。内部 `pack_single` 调用传空 `existing`（后处理只涉及新容器）。

---

## 数据流

```
JSON input
  │
  ├─ existing_containers[]  ──→  ExistingPlacement (from_json)
  │                                    │
  │                              build_load_from_existing()
  │                                    │
  │                              ContainerLoad (locked=true)
  │                                    │
  │                         ┌──────────┴──────────┐
  │                    阶段 A: all_loads      阶段 B: cl.placements
  │                         │                      │
  │                         │              pack_single(remaining, ct, existing)
  │                         │                      │
  │                         │           ┌──────────┼──────────┐
  │                         │         GEP        GLC        RGS/BSG
  │                         │       prefill    reconstruct  prefill
  │                         │       + EP       spaces       + EP/residual
  │                         │                      │
  │                         └──────────────────────┘
  │                                    │
  │                              extra (existing + new)
  │                                    │
  │                         覆盖 cl 的可变字段
  │                                    │
  └── boxes[]  ──→  remaining_ids  ──→  阶段 C: pack_single(remaining, ct, {})
                                                    │
                                                 新容器
```
