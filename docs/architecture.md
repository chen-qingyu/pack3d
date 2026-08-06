# 求解器架构

## 1. 算法总览

统一入口 `app::run()` 解析并校验输入后，由 `PackerBase::pack()` 模板方法驱动整个求解流程：`select_largest_fitting` 选车 → 调用虚函数 `pack_single()` 填充单容器 → 收箱 → `postprocess()` 后处理。四种算法只需实现各自的 `pack_single()`，由 `make_packer()` 按 `problem.algorithm` 选择：

- **`GEP`**（默认）— 极点贪心法：体积降序 + EP 优先填充。见 §3。
- **`GLC`** — 贪心前瞻构造：块装载 + beam 搜索。见 §4。
- **`RGS`** — 随机贪心搜索：EP-first-fit + 多策略排序 + Shaw 随机化，多起点采样择优。见 §5。
- **`BSG`** — 束搜索：宽度限制的启发式树搜索 + KPA 块合并。见 §6。

### 坐标系

右手坐标系：X 轴（长度，向右为正）、Y 轴（宽度，向后为正）、Z 轴（高度，向上为正），地板 Z=0。

---

## 2. 目标向量优化

### 2.1 字典序比较

目标不是加权和，而是字典序。优先级高的维度先比，打平才看下一维。四个维度固定为：

1. `min_container_count` — 容器数最少
2. `min_platform_split` — 平台拆分次数最少
3. `max_volume_rate` — 体积利用率最高
4. `min_group_split` — 组拆分次数最少

平台优化部分在线进行：BSG 的 beam 排序与 RGS 的评分以（体积, 剩余平台数）字典序倾向平台聚拢；其余平台与分组优化通过统一共享后处理完成（见 §8）。

---

## 3. GEP 极点贪心法

### 3.1 核心思想

箱子按体积降序排序，维护候选极点集合（初始为原点），逐个箱子尝试所有允许朝向在极点上放置，首 fit 即放，放置后生成 3 个新极点（右方、后方、上方）。

约束检查复用 `constraints.hpp`：边界、重叠、重量、支撑率、平台限制、路线顺序。

### 3.2 实现

`GepPacker::pack_single()` — 实现 `PackerBase` 虚函数，~130 行。选车和调度由 `PackerBase::pack()` 模板方法统一处理。

---

## 4. GLC 贪心前瞻构造

> 来源：求解三维装箱问题的多层启发式搜索算法. 计算机学报, 2012, 35(12):2553–2561

### 4.1 解决什么问题

原始 MLHS 瞄准**单容器三维装箱**：给定容器和箱子集合，正交放置、不重叠，最大化填充率，只有方向约束（C1）和稳定性约束（C2）。

本项目将其扩展为**多容器、多约束**场景。

### 4.2 整体架构：三层分解

- **调度层（Layer 1）** — `Packer`：多容器分配，对每种容器类型尝试装载所有剩余箱子，选最优者，反复迭代。
- **装载层（Layer 2）** — `Heuristic`：对单个容器内的箱子子集运行 GLC 块装载算法（简单块 + 空间栈 + 前瞻评估）。
- **数据层** — `SimpleBlock` (block.hpp), `Space`/`SpaceKind` (space.hpp) 为 GLC 专属类型，不在共用 types.hpp 中。
- **约束层（Layer 3）** — `constraints.hpp`：所有硬约束的纯函数实现，为装载层提供实时可行性判断。

### 4.3 论文原始 MLHS 核心思想

#### CLTRS 的痛点

前置工作 CLTRS 用整数拆分做树状搜索选块，两个问题：（1）浅层错选后无法挽回；（2）不同拆分路径重复计算。

#### MLHS 的思想转折

> **与其每层只保 1 个局部最优，不如每层保住一批 Top‑N 候选（beam），并对候选块做浅层前瞻评估，用"最终能填满多少"反选当前块。**

本质是 beam-style 多层展开 + 受限深度评估：第 i 层不只扩展 1 个节点，而是用堆留住最多 MaxHeap 个最优部分解，往前探 d 步后回填到目标层。

#### 论文参数

| 参数        |    值 | 作用               |
| ----------- | ----: | ------------------ |
| N           |    16 | 候选裁到前 N       |
| maxD        |     2 | 最大前瞻深度       |
| MaxDepth    |     6 | 分层搜索总次数     |
| MaxHeap     |     6 | 每层堆容量         |
| MinFillRate |  0.98 | 复合块填充率门槛   |
| MaxTimes    |     5 | 复合块迭代轮次上限 |
| MaxBlocks   | 10000 | 块表上限           |

使用渐进参数档：MaxDepth 2->7，Effort 1->243，受 120s 总时间限制。

### 4.4 简单块与剩余空间栈

#### 简单块

用**同种箱子 + 同朝向**堆叠成 nx×ny×nz 的致密长方体作为基本投放单元。块内所有箱子的 platform、group 必须一致。仅实现了简单块，未做论文中的复合块，因为复杂约束下复合块的生成和评估都非常麻烦，且简单块已能提供足够的候选多样性。

#### 剩余空间栈

放置一个块后，将未填充空间确定性切成至多 3 个子空间（上方、右方、后方），按固定规则入栈。通过 parent_id 追踪空间来源，支持 TransferSpace 碎片回收——当栈顶空间无可行块时，尝试将其合并给同次划分的兄弟空间。

### 4.5 容器内装载

统一使用 **Beam 模式（`pack_beam`）**：每步对候选块做多轮精炼——模拟放置后用贪心完成（`greedy_complete`）评估最终状态作为 fitness（通过 `compare_local_scores` 多目标比较），排序后裁半/截断。最后直接取 beam 精炼胜出块 `candidates.front()`，放置前做多目标提前停止检查。

前瞻评估分两级：`greedy_complete` 对前几个候选做一步前瞻后选最优（通过 `pick_best_block`, eval_width=4）；`complete_largest` 纯贪心填充到底，用于评估最终得分。

不再保留贪心基线——beam 精炼自给自足。

### 4.6 调度与约束

调度层已提取到 `PackerBase::pack()` 模板方法中（统一大优先选车 + 单容器循环 + 后处理），GLC 仅实现 `pack_single()` 单容器填充。

约束检查在块放置时逐箱模拟检查：边界、重叠、重量、支撑率、平台数量限制、路线顺序、堆码层数（max_stack）、单箱承重（max_load）、发标限制（tender_limit）。所有检查复用现有的纯函数约束实现（constraints.hpp）。

### 4.7 当前实现 vs 论文原始方案

| 维度     | 原始 MLHS           | 当前实现（GLC）                              |
| -------- | ------------------- | -------------------------------------------- |
| 块类型   | 简单块 + 复合块     | 仅简单块                                     |
| 块选择   | 多层搜索 + 前瞻 d≤2 | greedy_complete + beam 精炼                  |
| 渐进参数 | 6 档逐步加深        | 单次运行                                     |
| 约束     | C1 + C2             | 方向+支撑+重量+平台+路线+堆码+承重+发标+时限 |
| 容器     | 单容器              | 多容器统一调度                               |
| 目标     | 最大化填充率        | 字典序多目标（全部 4 维）                    |

---

## 7. 四算法对比

| 维度     | GEP                     | GLC                                | RGS                                     | BSG                       |
| -------- | ----------------------- | ---------------------------------- | --------------------------------------- | ------------------------- |
| 核心策略 | 体积降序 + EP 优先填充  | 块装载 + beam 搜索 + 前瞻评估      | EP-first-fit + 多策略排序 + Shaw 随机化 | 束搜索 + KPA 块合并       |
| 搜索粒度 | 单箱级                  | 块级（多箱一次放置）               | 单箱级                                  | 块级（多箱一次放置）      |
| 容器分配 | 统一大优先调度 + 后处理 | 统一大优先调度 + 后处理            | 统一大优先调度 + 后处理                 | 统一大优先调度 + 后处理   |
| 目标优化 | 后处理完成              | 装载层多目标打分 + 后处理完成      | 体积率评分 + 后处理完成                 | 体积率最大化 + 后处理完成 |
| 时限检查 | 基类主循环 check_time   | 基类主循环 + 装载层双重 check_time | 每迭代 check_time                       | 单容器内 check_time       |
| 复杂度   | O(n·p)，n=箱子，p=极点  | 较高（beam 展开 + 前瞻评估）       | O(M·n·e)，M=迭代数，e=EP 数             | 高（beam 展开 + 块表）    |
| 适用场景 | 通用、快速、简单场景    | 填充率要求高、可接受更长计算时间   | 多箱型场景，需要探索多样化布局          | 单容器填充率极高场景      |

---

## 6. RGS 随机贪心搜索

> 来源：Heßler, Hintsch, Wienkamp. _A Fast Optimization Approach For A Complex Real-Life 3D Multiple Bin Size Bin Packing Problem_. arXiv:2410.01445v1, 2024.
> 算法对应论文 §4（插入启发式）、§5（RGS 框架）、§6（多 ULD 策略）。

### 5.1 论文概览

| 项目     | 内容                                                             |
| -------- | ---------------------------------------------------------------- |
| 英文标题 | A Fast Optimization Approach For A Complex Real-Life 3D MBSBPP   |
| 中文译名 | 复杂真实场景三维多箱型装箱问题快速优化方法                       |
| 作者     | Katrin Heßler, Timo Hintsch, Lukas Wienkamp（德铁信可/Schenker） |
| arXiv    | 2410.01445v1, 2024-10-02                                         |
| 应用场景 | 航空货运 ULD（航空托盘/集装箱）装载                              |
| 核心贡献 | 边缘禁放+底托+垫料等真实约束；网格加速+RGS；非长方体ULD支持      |

**本实现的简化**：不做斜面 ULD、边缘禁放、底托、垫料、重心优化、倾斜物品。仅保留长方体 ULD + 支撑/堆叠/重量/平台/路线约束。

### 5.2 核心思想

> **单次插入用 first-fit 贪心，通过多策略排序 + Shaw 随机化产生多样化布局，多起点采样后择优。**

RGS 不是局部搜索——不需要邻域算子。它本质是构造式搜索（constructive search）：每次用不同顺序构造解，保留最佳。

### 5.3 整体架构

调度层已提取到 `PackerBase::pack()`（统一大优先选车 + 单容器循环 + 后处理）。RGS 仅实现 `pack_single()`：

````
RgsPacker::pack_single(items, ct)
  │
  ├─ 5 种排序策略 × M 次迭代
  ├─ 首次 ρ=0（确定性），后续 ρ=0.5（Shaw 随机化）
  └─ 评分用纯体积率 score = volume_rate()，择优
```

### 5.4 排序策略（§4.2）

| 策略                        | 含义                  | 特点              |
| --------------------------- | --------------------- | ----------------- |
| StackabilityCumulatedVolume | 可堆叠优先 + 总体积   | 论文最强 baseline |
| StackabilityHighestVolume   | 可堆叠优先 + 最大单件 | 也稳定            |
| CumulatedVolume             | 按总体积              | 偏利用率          |
| HighestVolume               | 按最大单件            | 先放大件          |
| Random                      | 完全随机              | 救极少数死局      |

相同箱型归入 IdenticalGroup；z 高度相交 + 同堆叠性的归入 SimilarGroup，合并后取交集为组目标高度。组间按策略排序，组内按体积降序。

展开到加载序列时，朝向锁定到组高度（§4.2.3）：只保留 `dz ∈ 组目标高度集合` 的朝向，确保同组物品以相同高度放置，形成连续支撑层。

### 5.5 Shaw 随机化（§4.2.2）

```text
第 j 个位置 = ceil( y_j^(1/ρ) × (n − j + 1) )
````

- ρ → 0：几乎不动（接近原排序）
- ρ = 0.5：中等扰动
- ρ = 1：完全随机

每次调用使用原子计数器生成不同种子，确保不同 RGS 迭代产生不同序列。

### 5.6 插入启发式（Alg1 + Alg3 + Alg4）

```
insertion_heuristic(items, ctype, criterion, rho):
  EP = {(0,0,0)}
  order = BuildOrder(items, criterion, rho)
  for (box, orients) in order:
    for ep in EP sorted by (z,y,x):
      for orient in orients:
        if can_place(box, orient, ep):   // 四道门检查
          commit_placement(box, orient, ep)
          break
```

**四道门**：边界 → 网格碰撞 → 支撑/堆叠性 → 重量/平台/路线。

### 5.7 EP 生成（Alg3 + Alg4）

每次放置后，对每个投影方向 j ∈ {x, y, z}（非可堆叠时跳过 z）和每个第二方向 d2 ≠ j，生成种子点：

```text
p_j = pos[j] + size[j]
p_d2 = pos[d2] + size[d2]
```

然后沿 d2 方向投影（Alg4）：向 −d2 扫描，找第一个未被阴影遮挡的已放物品，取其后表面为 EP；若无物品阻挡则取箱壁。

额外在顶部中心 `(pos.x, pos.y, pos.z + dz)` 生成一个 EP（仅可堆叠）。

### 5.8 ULD 评分

```text
score = volume_rate
```

评分简化为纯体积率。原论文的难装件惩罚在多 ULD 类型时有用，但当前统一大优先选车策略下，调度层已不依赖此评分选容器。

### 5.9 网格加速（§4.4.4）

以所有箱子平均边长为 cell_size，将已放物品注册到 3D 网格。碰撞检测时只查候选位置覆盖的网格单元内的物品，避免 O(N) 全量扫描。

### 5.10 数据结构

```
EpContext（rgs:: 内部）
  ├─ grid_cell_size        网格单元大小
  ├─ grid                  3D 网格 (cell → placement indices)
  └─ extreme_points        EP 集合（按 z,y,x 升序）

ContainerLoad（共享）
  ├─ placements            放置列表（含 osize 预计算尺寸）
  ├─ used_volume / total_weight
  ├─ platforms / groups / platform_x_min / platform_x_max
  └─ type                  指向 ContainerType
```

### 5.11 参数

| 参数       |  值 | 作用              |
| ---------- | --: | ----------------- |
| Mmin       |  10 | 总迭代下限        |
| Mmax       | 500 | 总迭代上限        |
| 每策略下限 |   2 | 确定性 + 1 次随机 |
| ρ          | 0.5 | Shaw 扰动强度     |
| 降级阈值   |  20 | 最后 ULD 箱子数   |

---

---

## 8. 共享后处理

`postprocess.cpp` 提供两个后处理阶段，所有算法共享，通过 `pack_single()` 回调重装：

1. **`repack_last_smaller`**：把最后一个容器尝试用更小容器类型重装（等价于 downsize）
2. **`reduce_platform_splits`**：把分散在多个容器中的同平台箱子并入某个已有容器（不新增容器，优先并入剩余空间最大的尾车），减少平台拆分。合并 trial 先尝试把捐献箱插入目标现有布局，失败则重排整个目标容器（目标自身箱子 + 捐献箱一起重新装载，碎片化布局也能容纳）。

后处理在 `PackerBase::pack()` 末尾统一调用。

---

## 9. 共享数据结构优化

### Placement.osize

`Placement` 新增 `OrientedSize osize` 字段，放置时预计算朝向后的实际尺寸。消除了 `check_overlap`、`check_support` 中反复的 `box_type_map.at().size.orient()` 调用。

- `check_overlap` 签名简化为 `(pos, osize, existing)`，不再需要 `box_type_map` 参数。
- `check_support` 中支撑矩形检测使用 `pl.osize`，不再需要 `box_type_map` 参数。

### 承重约束（max_stack / max_load）

`BoxType` 新增 `max_stack` / `max_load`（与 `allowed_orientations` 对齐的 optional 向量，标量输入广播到全部朝向），任一箱型有非空值即启用对应约束（presence-based）。

- `Placement` 新增内部字段 `stack_level` / `supported_load`（不序列化到输出），由 `constraints.cpp` 的 `check_stack_constraints`（只读预检）、`apply_stack_state`（放置后副作用）、`recompute_stack_state`（任意顺序重建 + 校验，用于 resume / 后处理合并 / 预校验）维护。
- 三个承重约束（`support_rate` / `max_stack` / `max_load`）相互独立、可同时开启。`support_rate = 0` 时允许悬空放置，悬空箱不计入堆叠柱，可能绕过 `max_stack` / `max_load`。
- BSG 因通块可能带 z 间隙（先放悬空箱再放下方箱），逐叶增量检查会漏判，`can_place_block` 在放置叶子后整体 `recompute_stack_state` 校验；GEP / GLC / RGS 自底向上放置，使用增量预检 + 提交后 `apply_stack_state`。
