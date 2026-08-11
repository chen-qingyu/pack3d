# 装托（Palletizing）

> 两级流水线：**散件（`loose:true`）→ 装托 → 装车**；普通箱子直接散装上车。
> 核心思想：**托盘即装箱单元**。装托阶段把托盘当小容器，用与装车完全相同的算法与约束链（`pack_single`）把散件码上托盘；随后每个托盘改写成一个"不可再叠放、可 90° 平面旋转"的虚拟箱，交给现有求解器装车——**装车零改动**，路线/重量/支撑/tender/后处理等既有约束全部免费生效。

## 1. 使用

### 1.1 输入

```json
{
  "pallet_types": [
    {
      "id": "pt1200",
      "sx": 1200,
      "sy": 1000,
      "sz": 150,
      "payload": 1000,
      "max_height": 1500,
      "self_weight": 30
    }
  ],
  "box_types": [
    {
      "id": "loose",
      "sx": 60,
      "sy": 40,
      "sz": 30,
      "allowed_orientations": ["xyz"],
      "loose": true
    }
  ]
}
```

| 配置       | 位置                              | 默认  | 说明                                                                                  |
| ---------- | --------------------------------- | ----- | ------------------------------------------------------------------------------------- |
| 启用装托   | 顶层 `pallet_types`               | 无    | 任一存在即启用；`id`/`sx`/`sy`/`sz`/`payload`/`max_height` 必填，`self_weight` 默认 0 |
| 散件标记   | `box_types.loose`                 | false | true = 散件（装托）；false = 普通箱子（直接装车）                                     |
| 装托支撑率 | `constraints.pallet_support_rate` | 1.0   | 托盘上箱子底面支撑率下限（装托专用，与装车 `support_rate` 独立）                      |
| 装托兜底   | `constraints.pallet_fallback`     | false | 散件装不进任何托盘：false = 未装箱报错（partial）；true = 降级散装上车                |

- **重量**：装托模式强制所有箱子带 `weight`、所有容器带 `payload`，否则 `invalid`。
- **校验**：`pallet_types` id 唯一、`loose: true` 但无 `pallet_types` → `invalid`；有 `pallet_types` 但无散件 → 等价普通装箱。

### 1.2 行为

- **装托**：每轮对所有托盘类型试装，按（体积, 箱数）取优；同 `group` 优先整组独占一托（始终按 `platform+group` 分组），装不下的退混合托（混合兜底也按站点分桶）。**不同站点的货物不能混装一托**：托盘恒为单站点（或全无站点），托盘单元带站点参与路线约束。托盘即小容器——不许悬挑、堆高 ≤ `max_height`（装载限高，不含托盘自身）、每托承重 ≤ `payload` 自动满足。
- **装车**：托盘改写为虚拟箱——`max_stack=[1,1]`（其上不可放箱，单向不叠托）、仅 XY 平面 90° 旋转、重量含托盘自重；与普通箱子一起走现有求解器，主目标仍是用车数最少。
- **时间**：装托分走 `min(20%×time_limit, 15s)`，剩余归装车。

| 结果                                    | 情形                                  |
| --------------------------------------- | ------------------------------------- |
| `complete`                              | 全部散件装托 + 装车完成               |
| `partial` + violations "not palletized" | 有散件装不进任何托盘且 fallback=false |
| 降级散装进入装车阶段                    | 有散件装不进任何托盘且 fallback=true  |
| `partial`（该托盘未装箱）               | 托盘单元装不进任何车厢                |
| `timeout`（已装部分保留）               | 超时                                  |

### 1.3 输出

- `summary` 新增 `pallet_count` / `palletized_box_count` / `loose_box_count`；`packed_box_count` 保持散箱口径（托盘按内部箱数计）。
- `result.pallets`：每托含 `pallet_id` / `used_height` / `used_weight` / `volume_rate` / `groups` / `platforms` / `placements`；容器中托盘单元 `box_id` = `pallet_id`，可在 `pallets` 展开内部明细。
- 装不进托的散件（fallback=false）：`partial` + violations 说明。

## 2. 实现（维护者）

- **代码**：`pallet.hpp/cpp`（类型 io + 虚拟容器/箱型生成）、`palletizer.hpp/cpp`（装托循环 / 问题改写 / 输出展开）；入口 `app.cpp` 两级流水线；`io.cpp` 与 `input_schema.json` 负责解析/校验。
- **关键点**：
  1. 装托 packer 绑定 problem 副本：`support_rate = pallet_support_rate`，**清除 `route`/`platform_limit`**——否则小托盘内被强加车厢卸货顺序/站点数约束。
  2. 改写后**重建 `has_max_stack`/`has_max_load`**——虚拟箱自带 `max_stack`，解析时算出的标志不含它，"不叠托"会失效。
  3. `pack_single` 返回的 `load.type` 指向传入的临时容器，须在析构前消费。
  4. 无 `pallet_types` 分支**零新增求解调用**（RGS `s_call_id` 静态计数，防既有测试漂移）。

## 3. 测试与限制

- **测试**：`data/tests/test_pallet_*.json` + `tests/test_solver.cpp` 的 `[pallet]` 用例，`xmake run test "[pallet]"`（GEP 确定性断言）。已知 BSG 对超小场景可能多开一车（固有次优），测试只对 GEP 严格断言。
- **限制**：托盘数量上限未实现；托盘装不进车厢则该托未装箱。
- **resume（续装）已支持**：`existing_containers` 与装托可同时使用——已有放置锁定不动，新散件装托后与普通箱一起续塞进已有容器剩余空间（或开新容器）。
- **装车支撑率**：装托模式要求 `constraints.support_rate > 0`（预校验强制，否则托盘可在车厢内悬空）。
- **单站点规则（2026-08-10）**：不同站点的货物不能混装一托（整组试装与混合兜底均按站点分桶）。
