# 输出格式

JSON，顶层四个字段：

```json
{
  "status": string,
  "summary": { ... },
  "result": { ... },
  "violations": [ ... ]
}
```

> **字段恒存在（消费契约）**：所有字段无论对应功能是否启用都会输出，未启用时给合理默认值——`null`（`payload`/`used_weight`/`weight_rate`/`tender`/`weight`/`max_stack`/`max_load`/`group`/`platform`）、空数组（`violations`/`obstacles`/`facets`/`pallets`/`unpacked_boxes`/`platforms`/`groups`）、`false`（`loose`）。下游（web/server/SDK）**依赖此契约**，不再对缺失字段做防御（如 `?.`/`?? []`/`.get("x", [])`）。唯一例外：斜面 `facets[].dx/dy/dz` 按格式只输出非零截距（天然缺省），消费方须容忍缺键。

## `status` 状态枚举

| 值           | 含义                     |
| ------------ | ------------------------ |
| `"complete"` | 全部装箱完成             |
| `"partial"`  | 算法完成，部分箱子未装入 |
| `"timeout"`  | 时限中断，返回当前最优   |
| `"invalid"`  | 输入非法                 |

## `summary` 统计摘要

```json
{
  "elapsed_second": 14.6,
  "packed_box_count": 105,
  "unpacked_box_count": 7,
  "container_count": 1,
  "platform_split": 0,
  "volume_rate": 0.9274,
  "volume_rate_x": 0.9274,
  "group_split": 0,
  "pallet_count": 2,
  "palletized_box_count": 8,
  "loose_box_count": 4
}
```

| 字段                   | 说明                                                                                                                        |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `elapsed_second`       | 耗时（秒）                                                                                                                  |
| `packed_box_count`     | 已装箱数（装托模式按散箱口径，托盘按内部箱数计）                                                                            |
| `unpacked_box_count`   | 未装箱数                                                                                                                    |
| `container_count`      | 使用容器数                                                                                                                  |
| `platform_split`       | 站点拆分总次数（0=每个站点只在一个容器中）                                                                                  |
| `volume_rate`          | 各容器平均体积利用率（0-1），口径 = 装箱体积 / 可用容积（物理总容积 − 障碍物 − 斜面楔形）                                   |
| `volume_rate_x`        | 各容器平均 X 方向体积利用率（0-1），口径 = 装箱体积 / X 方向 slab 可用容积（[0, used_x]×[0,sy]×[0,sz] − 障碍物 − 斜面楔形） |
| `group_split`          | 组拆分总次数（0=每组只在一个容器中）                                                                                        |
| `pallet_count`         | 托盘单元数（未启用装托恒为 0）                                                                                              |
| `palletized_box_count` | 已装托的散件箱数（未启用装托恒为 0）                                                                                        |
| `loose_box_count`      | 直接装车箱数：普通箱 + fallback 降级散件（未启用装托恒为 0）                                                                |

## `result` 装箱结果

```json
{
  "containers": [
    {
      "type_id": "big",
      "sx": 1300,
      "sy": 1100,
      "sz": 600,
      "payload": 100000.0,
      "used_volume": 216125000,
      "used_weight": 20.0,
      "volume_rate": 0.2519,
      "volume_rate_x": 0.262,
      "weight_rate": 0.0002,
      "packed_count": 2,
      "platforms": ["P1"],
      "groups": ["A"],
      "tender": 1,
      "obstacles": [],
      "facets": [],
      "placements": [
        {
          "box_id": "pt1200#1",
          "box_type_id": "pt1200#1",
          "x": 0,
          "y": 0,
          "z": 0,
          "dx": 1200,
          "dy": 1000,
          "dz": 180,
          "orientation": "xyz",
          "weight": 10.0,
          "platform": "P1",
          "group": "A"
        },
        {
          "box_id": "a1",
          "box_type_id": "big_box",
          "x": 1200,
          "y": 0,
          "z": 0,
          "dx": 50,
          "dy": 50,
          "dz": 50,
          "orientation": "xyz",
          "weight": 10.0,
          "platform": "P1",
          "group": "A"
        }
      ]
    }
  ],
  "pallets": [
    {
      "pallet_id": "pt1200#1",
      "type_id": "pt1200",
      "sx": 1200,
      "sy": 1000,
      "sz": 150,
      "payload": 1000,
      "max_height": 1500,
      "used_height": 30,
      "used_weight": 10.0,
      "volume_rate": 0.001,
      "groups": ["A"],
      "platforms": ["P1"],
      "placements": [
        {
          "box_id": "l1",
          "box_type_id": "loose",
          "x": 0,
          "y": 0,
          "z": 0,
          "dx": 60,
          "dy": 40,
          "dz": 30,
          "orientation": "xyz",
          "weight": 10.0,
          "platform": "P1",
          "group": "A"
        }
      ]
    }
  ],
  "box_types": [
    {
      "id": "big_box",
      "sx": 50,
      "sy": 50,
      "sz": 50,
      "allowed_orientations": ["xyz"],
      "loose": false,
      "max_stack": [3],
      "max_load": [200.0],
      "weight": null,
      "group": null,
      "platform": null
    },
    {
      "id": "loose",
      "sx": 60,
      "sy": 40,
      "sz": 30,
      "allowed_orientations": ["xyz"],
      "loose": true,
      "max_stack": [null],
      "max_load": [null],
      "weight": null,
      "group": null,
      "platform": null
    },
    {
      "id": "pt1200#1",
      "sx": 1200,
      "sy": 1000,
      "sz": 180,
      "allowed_orientations": ["xyz", "yxz"],
      "loose": false,
      "max_stack": [1, 1],
      "max_load": [null, null],
      "weight": null,
      "group": "A",
      "platform": "P1"
    }
  ],
  "unpacked_boxes": ["b30", "b31"]
}
```

容器数组顺序即装车顺序。`null` 表示该维度不适用（如重量未配置时 `used_weight`/`weight_rate` 为 null；有效箱子未设置站点/分组时 `platform`/`group` 为 null）。混组托盘的虚拟箱 `group` 亦为 null，其分组见对应 `pallets[].groups`（按 `box_id == pallet_id` 关联）。

`volume_rate_x` 为容器 X 方向口径的体积利用率，与 `volume_rate` 对齐、仅把"整个容器"换成"实际使用的 X 方向 slab"：分母 = slab [0, used_x]×[0,sy]×[0,sz] 的可用容积 = `used_x·sy·sz` − 障碍物（slab 内部分）− 斜面楔形（slab 内部分），分子 = 装箱体积（同 `volume_rate`）。其中 `used_x` = 所有箱子 `x+dx` 的最大值（含续装已有放置）；`used_x` 达容器全长时本值与 `volume_rate` 相等。容器未装任何箱时为 0。`summary.volume_rate_x` 为各容器该值的平均。

`obstacles` 为本容器实例的障碍物（从容器类型继承，自包含），结构与输入一致；未配置时为 `[]`。

`facets` 为本容器实例的斜面（从容器类型继承，自包含），结构与输入一致（`dx`/`dy`/`dz` 恰好两个非零截距）；未配置时为 `[]`。

`tender` 为该容器所属 tender 的序号（1-based）：容器按有效 `group` 连通，每个连通分量即一个 tender，按容器顺序首次出现编号。因输入 `group` 采用三选一来源规则（见 input.md），全无时 `tender` 为 `null`，有 group 时相关容器带数字编号。如容器 A{g1,g2}、B{g2,g3}、C{g3,g4}、D{g5}，则 A/B/C 的 `tender` 均为 1，D 为 2。

`pallets` 为托盘明细数组，**恒输出**（未启用装托时为空数组 `[]`）。启用装托时，容器 placement 中托盘单元 `box_id` = `pallet_id`，其内部散件在 `pallets` 中展开。每托字段：

| 字段           | 说明                                        |
| -------------- | ------------------------------------------- |
| `pallet_id`    | 托盘实例 ID（形如 `pt1200#1`）              |
| `type_id`      | 托盘类型 ID                                 |
| `sx`/`sy`/`sz` | 托盘自身尺寸                                |
| `payload`      | 托盘装载承重上限（货物总重，不含托盘自重）  |
| `max_height`   | 装载限高（货物堆高上限，不含托盘自身高度）  |
| `used_height`  | 货物堆高（不含托盘自身）                    |
| `used_weight`  | 货物总重（不含托盘自重）                    |
| `volume_rate`  | 体积利用率 = 货物体积 / (sx·sy·max_height)  |
| `groups`       | 托盘内去重 group 列表                       |
| `platforms`    | 托盘内去重 platform 列表                    |
| `placements`   | 托盘内散件放置列表（同容器 placement 结构） |

装托输入与行为详见 [architecture.md](architecture.md) §3。

`result.box_types` 与输入 `box_types` 结构一致，并回显输入中配置的 `max_stack` / `max_load`、`weight`、`group`、`platform`（承重字段与 `allowed_orientations` 对齐的数组，未配置为 `null`，字段恒存在）；装托模式下额外包含虚拟托盘箱型。箱型级 `group/platform` 会同时以有效值出现在对应 placement 中；箱子级模式下箱型字段为 `null`，placement 保留各实例值。

装托模式下的虚拟托盘箱型是合成类型；单 group 托盘的箱型字段与 placement 使用相同的有效值。混合 group 托盘的虚拟箱 `group` 为 `null`（完整分组只存在于 `result.pallets[].groups`，下游用 `placement.box_id == pallet.pallet_id` 关联识别）。混合托盘内部的 group 集合仅用于求解阶段的约束和容器级聚合，不新增 placement 输出字段。

placement 字段说明：

| 字段           | 说明                                                                                     |
| -------------- | ---------------------------------------------------------------------------------------- |
| `box_id`       | 箱子实例 ID                                                                              |
| `box_type_id`  | 箱子类型 ID                                                                              |
| `x`/`y`/`z`    | 放置位置（min corner）                                                                   |
| `dx`/`dy`/`dz` | 朝向后的实际尺寸（沿容器轴向）                                                           |
| `orientation`  | 朝向（`xyz`/`xzy`/...）                                                                  |
| `weight`       | 箱子重量，未设置时为 null                                                                |
| `platform`     | 有效站点 ID（配送停靠点），未设置时为 null                                               |
| `group`        | 有效分组 ID，未设置时为 null；混组托盘的虚拟箱此处为 null，完整分组见 `pallets[].groups` |

## `violations` 说明（非 `complete` 时可能出现）

`status: "invalid"` 时为输入校验违规：

```json
["duplicate box id: b1", "unknown box_type_id 'foo' for box b2"]
```

`status: "partial"` / `"timeout"` 时给出未装箱原因说明：

```json
[
  "1 box(es) not packed: exceeds tender_limit (max 1 containers per tender), groups: g1"
]
```

- 启用 `tender_limit` 时：若未装箱子的 `group` 即使开新容器也会超限，会给出 `exceeds tender_limit` 说明（含 group 列表）。
- 其余未装箱子给出通用说明：`timeout` 时为 `time limit exceeded`，`partial` 时为 `no feasible placement`。
- `complete` 与 `invalid` 之外都可能有 `violations`，每条为一个完整可读的描述。
