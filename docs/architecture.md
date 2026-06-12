# 求解器架构

## 1. 整体流程

```
solve()
  │
  ├─ make_initial_state()         ── 初始状态（排序待装箱子）
  │
  ├─ construct_solution(state)    ── 主循环
  │     │
  │     ├─ while 有剩余箱子
  │     │   ├─ check_time()       ── 超时检测
  │     │   ├─ place_next_box()   ── 选最优位置放一个箱子
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
  │   ├─ 无解且有时 -> multi_start_solve() 多起点重试
  │   └─ 完全无解 -> 返回 no_solution
  │
  └─ build_solution()            ── 组装输出
```

### 数据结构

```
SearchState
  ├─ remaining_boxes       待装箱列表（搜索过程中逐个移除）
  ├─ open_containers       已打开的容器列表
  │   └─ ContainerLoad
  │       ├─ placements    箱子的放置列表
  │       ├─ extreme_points 候选位置极点（SGEP 算法核心）
  │       ├─ used_volume   已用体积
  │       ├─ platforms     本容器涉及的平台集合
  │       ├─ groups        本容器涉及的分组集合
  │       └─ platform_x_max / min  路线 X 跟踪
  ├─ best_feasible         当前最优可行解（快照）
  ├─ group_spread          分组->容器实例映射（tender_limit 用）
  ├─ current_objective     当前目标缓存
  └─ infeasible            是否被启发式判定为不可行
```

---

## 2. 开新容器策略

`open_new_container()` 在无法放入任何已有容器时被调用。

### 选择逻辑

```
1. 收集可用容器类型
2. 计算剩余箱子总体积
3. 按体积升序排列
4. 遍历：
   - 容积 >= 剩余体积 且 所有箱子维度都能放入 -> 选这个（最小可用）
   - 容积 >= 剩余体积 但维度放不下 -> 记住作为 fallback
5. 没找到完美匹配 -> 用 fallback（最大有足够容积的容器）
6. 创建新容器实例，从原点 (0,0,0) 开始
```

### 要点

- 容积优先：优先用刚好装下剩余体积的最小容器，避免浪费空间
- 维度检查：确保每个箱子在至少一个朝向下能放入（否则开了也白开）
- 回退机制：所有箱子维度都放不下时，退回最大的容器——尽量给后续放置留空间
- 新容器的第一个极点是原点 `(0,0,0)`，后续放置会生成更多极点

---

## 3. 目标向量优化

### 3.1 字典序比较

目标不是加权和，而是字典序。优先级高的维度先比，打平才看下一维。

默认优先级：

1. `min_container_count` — 容器数最少
2. `min_platform_count` — 平台总数最少
3. `max_volume_rate` — 体积利用率最高
4. `min_group_split` — 组拆分次数最少

### 3.2 投影机制

每次放置箱子时，求解器对所有候选位置投影目标向量，选投影最优的那个。

#### 投影流程

对每个候选（已有容器的极点 / 新容器原点）：

1. 从 current_objective 复制一份 proj
2. 根据此放置的影响，更新 proj：
   - 放入已有容器：无
   - 放入已有容器 + 引入新平台：platform +1
   - 放入已有容器 + 引入新组：group_split +1
   - 装填此容器后再也放不进其他箱子：container +1, platform +1, group_split +1
   - 开新容器：container +1, platform +1, group_split +1
   - 新容器装不下后续箱子 → 需要额外容器：container +N, platform +N, group_split +N

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

### 3.3 示例

目标：`[min_container_count, max_volume_rate]`

- 候选 A：放入已有大容器 -> container_count=2, volume_rate=0.8
- 候选 B：开新一个小容器 -> container_count=3, volume_rate=0.9

字典序先比 container_count：2 < 3 -> 选 A，即使 B 的 volume_rate 更高。

---

## 4. 多起点重试

当首次搜索未找到可行解时，`multi_start_solve()` 尝试不同的箱子排序策略，增加找到解的几率。

排序策略列表：

1. ByVolume 大体积优先（默认）
2. ByVolumeAsc 小体积优先
3. ByHeight 高箱子优先
4. ByPlatformThenVolume 按路线平台分组，再按体积
5. ByMixed 随机打乱（3 次）

对每个排序策略执行 2 次（ByMixed 3 次），
保留所有尝试中的最优解（字典序比较）。

只有全部装箱（`all_packed == true`）且优于当前 `best_feasible` 的结果才会被采纳。

---

## 5. 放置执行

选定最优放置后，`apply_placement` 执行以下操作：

1. 紧凑化 -> 向 Z-（地板）、Y-（墙面）、X-（车头）方向滑动，消除间隙
2. 记录 Placement（box_id, position, orientation）
3. 更新容器状态
4. 生成新极点，过滤极点
5. 更新 current_objective

### 极点生成

每次放置后产生 3 个候选极点：

- X 方向：`(pos.x + dx, pos.y, pos.z)`
- Y 方向：`(pos.x, pos.y + dy, pos.z)`
- Z 方向：`(pos.x, pos.y, pos.z + dz)`

极点经过过滤（移除超出边界、在已有箱子内部、无意义的重复点），排序后优先尝试低处（Z 小）。
