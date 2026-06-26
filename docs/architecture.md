# 求解器架构

## 1. 算法总览

`solve()` 根据 `problem_.algorithm.algorithm` 选择算法：

- **`GEP`**（默认）— 贪心极点算法：逐个箱子遍历极点找最优位置，逐容器打开。见 §2。
- **`GLC`** — 贪心前瞻构造：块装载 + 前瞻评估，全局调度器迭代分配容器。见 §4。
- **`RGS`** — 随机贪心搜索：EP-first-fit + 多策略排序 + Shaw 随机化，多起点采样择优。见 §6。

### 坐标系

右手坐标系：X 轴（长度，向右为正）、Y 轴（宽度，向后为正）、Z 轴（高度，向上为正），地板 Z=0。

---

## 2. GEP 贪心极点法

### 2.1 整体流程

```
Packer::pack()
  │
  ├─ make_initial_state()         ── 初始状态（排序待装箱子）
  │
  ├─ construct_solution(state)    ── 主循环
  │     │
  │     ├─ while 有剩余箱子
  │     │   ├─ check_time()       ── 超时检测
  │     │   ├─ Placer::place_next_box()   ── 选最优位置放一个箱子
  │     │   │   ├─ 遍历已有容器的极点，找最佳放置
  │     │   │   ├─ 惰性 fills_container 检测
  │     │   │   └─ 评估开新容器选项
  │     │   │
  │     │   └─ 若放不下：
  │     │       ├─ check_tender_limit()   ── 组分散阻断检测
  │     │       └─ open_new_container()   ── 开新容器
  │     │
  │     └─ update_best()          ── 全装完时记录最佳解
  │
  ├─ 收尾处理
  │   ├─ infeasible -> 返回失败
  │   ├─ all_packed -> 返回成功
  │   ├─ 部分装箱 -> 返回 best_feasible
  │   └─ 完全无解 -> 返回 no_solution
  │
  └─ build_solution()            ── 组装输出
```

### 2.2 数据结构

```
SearchState
  ├─ remaining_boxes       待装箱列表（搜索过程中逐个移除）
  ├─ open_containers       已打开的容器列表 (ContainerLoad)
  │       ├─ placements    箱子的放置列表
  │       ├─ used_volume   已用体积
  │       ├─ platforms     本容器涉及的平台集合
  │       ├─ groups        本容器涉及的分组集合
  │       └─ platform_x_max / min  路线 X 跟踪
  ├─ extreme_points        各容器的候选极点（instance_id -> 极点列表）
  ├─ best_feasible         当前最优可行解（快照）
  ├─ group_spread          分组->容器实例映射（tender_limit 用）
  ├─ current_objective     当前目标缓存
  └─ infeasible            是否被启发式判定为不可行
```

### 2.3 箱子排序策略

`make_initial_state()` 对待装箱子进行一次排序，之后不再重试。
排序策略固定为**按平台分组，同平台内按体积降序**：

1. 空平台箱子视为默认平台，与有平台箱子统一按平台名字典序分组
2. 同平台内按体积从大到小排序（大箱优先）
3. 使用 `stable_sort`，同体积箱子保持原始相对顺序

这种排序鼓励同平台箱子在搜索中被连续放置到同一容器中，有利于降低 `platform_split` 目标。

### 2.4 开新容器策略

`open_new_container()` 在无法放入任何已有容器时被调用。

#### 选择逻辑

```
1. 收集可用容器类型
2. 计算剩余箱子总体积
3. 按体积升序排列
4. 遍历：
   - 容积 >= 剩余体积 且 所有箱子维度都能放入 -> 选这个（最小可用）
   - 容积 >= 剩余体积 但维度放不下 -> 记住作为 fallback
5. 没找到完美匹配 -> 用 fallback（容积足够的最小容器）
6. 创建新容器实例，从原点 (0,0,0) 开始
```

#### 要点

- 容积优先：优先用刚好装下剩余体积的最小容器，避免浪费空间
- 维度检查：确保每个箱子在至少一个朝向下能放入（否则开了也白开）
- 回退机制：若不存在容积够装全部剩余箱子的容器类型，退回全局最大的容器；否则退回容积足够的最小容器
- 新容器的第一个极点是原点 `(0,0,0)`，后续放置会生成更多极点

### 2.5 放置执行

选定最优放置后，`apply_placement` 执行以下操作：

1. 记录 Placement（box_id, position, orientation）
2. 更新容器状态
3. 生成新极点，过滤极点
4. 更新 current_objective

#### 极点生成

每次放置后产生 3 个候选极点：

- X 方向：`(pos.x + dx, pos.y, pos.z)`
- Y 方向：`(pos.x, pos.y + dy, pos.z)`
- Z 方向：`(pos.x, pos.y, pos.z + dz)`

极点经过过滤（移除超出边界、在已有箱子内部、无意义的重复点），排序后优先尝试低处（Z 小）。

---

## 3. 目标向量优化

### 3.1 字典序比较

目标不是加权和，而是字典序。优先级高的维度先比，打平才看下一维。四个维度固定为：

1. `min_container_count` — 容器数最少
2. `min_platform_split` — 平台拆分次数最少
3. `max_volume_rate` — 体积利用率最高
4. `min_group_split` — 组拆分次数最少

### 3.2 GEP 投影机制

每次放置箱子时，GEP 对所有候选位置投影目标向量，选投影最优的那个。

#### 投影流程

对每个候选（已有容器的极点 / 新容器原点）：

1. 从 current_objective 复制一份 proj
2. 根据此放置的影响，更新 proj：
   - 放入已有容器：无
   - 放入已有容器 + 引入新平台：platform +1
   - 放入已有容器 + 引入新组：group_split +1
   - 装填此容器后再也放不进其他箱子：container +1, platform +1, group_split +1
   - 开新容器：container +1, platform +1, group_split +1
   - 新容器装不下后续箱子 -> 需要额外容器：container +N, platform +N, group_split +N

3. avg_volume_rate 的估算：
   - 放入已有容器：移除旧 rate，加入新 rate，重新平均
   - 开新容器：count+1，追加新容器的 rate

4. 用 compare_objectives(proj, best_proj) 选优

#### fills_container 惰性检测

当某个放置大概率会把容器"填死"（后续箱子放不进去），投影中提前计入额外容器的代价，让求解器在"撑满当前容器"和"开新容器"之间做出权衡。

检测方式：

1. 容积已满（cap_left <= 0）-> fills
2. 否则生成放置后的极点
3. 若无有效极点 -> fills
4. 否则检查每个剩余箱子：是否均无法放入任何极点 -> fills

### 3.4 GLC 的目标处理

GLC 在**调度层**和**装载层**均有多目标感知：

| 层级               | 机制                                             | 覆盖维度                  |
| ------------------ | ------------------------------------------------ | ------------------------- |
| **全局调度**       | 字典序目标投影（含未来平台/分组估算）            | 全部 4 维                 |
| **容器内 Beam**    | `greedy_complete` 用 `compare_local_scores` 评分 | platform / volume / group |
| **容器内最终选择** | 放置前多目标提前停止检查                         | platform / group          |

调度层投影对每种容器类型计算 `ObjectiveVector`，包含 `future_platforms` 和 `future_groups` 估算（类似已有的 `future` 容器数估算），通过 `compare_objectives` 字典序选最优容器类型。

装载层 `compare_local_scores` 按目标键比较 `platform_split` / `used_volume` / `group_count`，不含 `min_container_count`（单容器内不适用）。最终放置前做多目标提前停止：若放置候选块后完成态得分不优于当前态，停止装箱（避免引入多余平台/分组）。

目标：`[min_container_count, max_volume_rate]`

- 候选 A：放入已有大容器 -> container_count=2, volume_rate=0.8
- 候选 B：开新一个小容器 -> container_count=3, volume_rate=0.9

字典序先比 container_count：2 < 3 -> 选 A，即使 B 的 volume_rate 更高。

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

### 4.6 全局调度

统一迭代流程：维护剩余箱子集合 `remaining_ids`，每轮遍历所有容器类型，调用装载引擎打包，对每种容器类型投影 `ObjectiveVector`（含 `future` 容器数估算、`future_platforms`、`future_groups`），通过 `compare_objectives` 字典序选最优。移走已装箱子，更新 `container_type_usage_`，重复直到装完或超时或没有容器可用。

支持 `quantity_limit`、`check_time` 超时中断（调度层 + 装载层双重检查）、`tender_limit` 预检查。

### 4.7 约束检查

在块放置时逐箱模拟检查：边界、重叠、重量、支撑率、平台数量限制、路线顺序。所有检查复用现有的纯函数约束实现（constraints.hpp）。

### 4.8 当前实现 vs 论文原始方案

| 维度     | 原始 MLHS           | 当前实现（GLC）                    |
| -------- | ------------------- | ---------------------------------- |
| 块类型   | 简单块 + 复合块     | 仅简单块                           |
| 块选择   | 多层搜索 + 前瞻 d≤2 | greedy_complete + beam 精炼        |
| 渐进参数 | 6 档逐步加深        | 单次运行                           |
| 约束     | C1 + C2             | 方向+支撑+重量+平台+路线+发标+时限 |
| 容器     | 单容器              | 多容器统一调度                     |
| 目标     | 最大化填充率        | 字典序多目标（全部 4 维）          |

---

## 5. 三算法对比

| 维度     | GEP                               | GLC                                                               | RGS                                     |
| -------- | --------------------------------- | ----------------------------------------------------------------- | --------------------------------------- |
| 核心策略 | 逐箱贪心 + 极点搜索               | 块装载 + beam 搜索 + 前瞻评估                                     | EP-first-fit + 多策略排序 + 随机重启    |
| 搜索粒度 | 单箱级                            | 块级（多箱一次放置）                                              | 单箱级                                  |
| 容器分配 | 随装箱动态开新容器                | 全局调度器统一分配                                                | 全局调度（最稀缺优先）+ 后处理重装/合并 |
| 目标优化 | 每步字典序投影，覆盖全部 4 个维度 | 调度层字典序投影 + 装载层多目标打分 + 提前停止，覆盖全部 4 个维度 | ULD 评分（体积率 − 难装件惩罚），单目标 |
| 时限检查 | 每步 check_time                   | 调度层 + 装载层双重 check_time                                    | 每迭代 check_time                       |
| 复杂度   | O(n·m·p)，n=箱子，m=容器，p=极点  | 较高（beam 展开 + 前瞻评估）                                      | O(M·n·e)，M=迭代数，e=EP 数             |
| 适用场景 | 通用、轻量、约束简单的快速解      | 填充率要求高、可接受更长计算时间                                  | 多箱型场景，需要探索多样化布局          |

---

## 6. RGS 随机贪心搜索

> 来源：Heßler, Hintsch, Wienkamp. _A Fast Optimization Approach For A Complex Real-Life 3D Multiple Bin Size Bin Packing Problem_. arXiv:2410.01445v1, 2024.
> 算法对应论文 §4（插入启发式）、§5（RGS 框架）、§6（多 ULD 策略）。

### 6.1 论文概览

| 项目     | 内容                                                             |
| -------- | ---------------------------------------------------------------- |
| 英文标题 | A Fast Optimization Approach For A Complex Real-Life 3D MBSBPP   |
| 中文译名 | 复杂真实场景三维多箱型装箱问题快速优化方法                       |
| 作者     | Katrin Heßler, Timo Hintsch, Lukas Wienkamp（德铁信可/Schenker） |
| arXiv    | 2410.01445v1, 2024-10-02                                         |
| 应用场景 | 航空货运 ULD（航空托盘/集装箱）装载                              |
| 核心贡献 | 边缘禁放+底托+垫料等真实约束；网格加速+RGS；非长方体ULD支持      |

**本实现的简化**：不做斜面 ULD、边缘禁放、底托、垫料、重心优化、倾斜物品。仅保留长方体 ULD + 支撑/堆叠/重量/平台/路线约束。

### 6.2 核心思想

> **单次插入用 first-fit 贪心，通过多策略排序 + Shaw 随机化产生多样化布局，多起点采样后择优。**

RGS 不是局部搜索——不需要邻域算子。它本质是构造式搜索（constructive search）：每次用不同顺序构造解，保留最佳。

### 6.3 整体架构

```
Packer::pack()
  │
  ├─ compute_penalty_denom()     ── 全局预计算难装件惩罚分母
  │
  ├─ 主循环 Alg7:
  │   ├─ select_next_uld()       ── 最稀缺优先选容器类型
  │   ├─ rgs_single_uld()        ── 单 ULD 多起点搜索 (Alg5)
  │   │   ├─ 5 种排序策略 × M 次迭代
  │   │   ├─ 首次 ρ=0（确定性），后续 ρ=0.5（Shaw 随机化）
  │   │   └─ score_uld() 评分，择优
  │   └─ 移除已装箱子
  │
  ├─ 后处理 1: 大容器→小容器重装
  ├─ 后处理 2: 合并分散的同平台/同组物品
  └─ 后处理 3: 降级最后一个 ULD
```

### 6.4 排序策略（§4.2）

| 策略                        | 含义                  | 特点              |
| --------------------------- | --------------------- | ----------------- |
| StackabilityCumulatedVolume | 可堆叠优先 + 总体积   | 论文最强 baseline |
| StackabilityHighestVolume   | 可堆叠优先 + 最大单件 | 也稳定            |
| CumulatedVolume             | 按总体积              | 偏利用率          |
| HighestVolume               | 按最大单件            | 先放大件          |
| Random                      | 完全随机              | 救极少数死局      |

相同箱型归入 IdenticalGroup；z 高度相交 + 同堆叠性的归入 SimilarGroup，合并后取交集为组目标高度。组间按策略排序，组内按体积降序。

展开到加载序列时，朝向锁定到组高度（§4.2.3）：只保留 `dz ∈ 组目标高度集合` 的朝向，确保同组物品以相同高度放置，形成连续支撑层。

### 6.5 Shaw 随机化（§4.2.2）

```text
第 j 个位置 = ceil( y_j^(1/ρ) × (n − j + 1) )
```

- ρ → 0：几乎不动（接近原排序）
- ρ = 0.5：中等扰动
- ρ = 1：完全随机

每次调用使用原子计数器生成不同种子，确保不同 RGS 迭代产生不同序列。

### 6.6 插入启发式（Alg1 + Alg3 + Alg4）

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

### 6.7 EP 生成（Alg3 + Alg4）

每次放置后，对每个投影方向 j ∈ {x, y, z}（非可堆叠时跳过 z）和每个第二方向 d2 ≠ j，生成种子点：

```text
p_j = pos[j] + size[j]
p_d2 = pos[d2] + size[d2]
```

然后沿 d2 方向投影（Alg4）：向 −d2 扫描，找第一个未被阴影遮挡的已放物品，取其后表面为 EP；若无物品阻挡则取箱壁。

额外在顶部中心 `(pos.x, pos.y, pos.z + dz)` 生成一个 EP（仅可堆叠）。

### 6.8 ULD 评分

```text
score = volume_rate − penalty

penalty = Σ(unloaded 中难装件体积 × (ULD类型数 − 该件可入ULD数)) / denom
```

单 ULD 类型时 penalty 为 0，退化为纯体积率。难装件惩罚鼓励把"只有大 ULD 装得下"的箱子优先装入大 ULD。

### 6.9 网格加速（§4.4.4）

以所有箱子平均边长为 cell_size，将已放物品注册到 3D 网格。碰撞检测时只查候选位置覆盖的网格单元内的物品，避免 O(N) 全量扫描。

### 6.10 数据结构

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

### 6.11 参数

| 参数       |  值 | 作用              |
| ---------- | --: | ----------------- |
| Mmin       |  10 | 总迭代下限        |
| Mmax       | 500 | 总迭代上限        |
| 每策略下限 |   2 | 确定性 + 1 次随机 |
| ρ          | 0.5 | Shaw 扰动强度     |
| 降级阈值   |  20 | 最后 ULD 箱子数   |

---

## 7. 共享数据结构优化

### Placement.osize

`Placement` 新增 `OrientedSize osize` 字段，放置时预计算朝向后的实际尺寸。消除了 `check_overlap`、`check_support` 及 GEP EP 过滤中反复的 `box_type_map.at().size.orient()` 调用。

- `check_overlap` 签名简化为 `(pos, osize, existing)`，不再需要 `box_type_map` 参数。
- `check_support` 中支撑矩形和四角检测均使用 `pl.osize`，仅保留 `box_type_map` 用于读取 `stackable`。

### BoxType.stackable

新增 `bool stackable` 字段，默认 `true`。`check_support` 检测到下方物品 `stackable == false` 时直接拒绝放置。
