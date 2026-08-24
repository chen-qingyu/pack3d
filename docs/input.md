# 输入格式

JSON，schema 见 `data/input_schema.json`。必填顶层字段：`container_types`、`box_types`、`boxes`。输入示例见 [data/demo.json](../data/demo.json)。

## 坐标系

约定程序统一采用右手坐标系，定义如下：

- X 轴：长度，向右为正方向。
- Y 轴：宽度，向后为正方向。
- Z 轴：高度，向上为正方向。

## 容器类型 `container_types`

```json
{
  "id": "big",
  "sx": 110,
  "sy": 50,
  "sz": 50,
  "payload": 50000.0,
  "quantity_limit": null,
  "obstacles": [{ "x": 0, "y": 0, "z": 40, "dx": 3, "dy": 50, "dz": 10 }]
}
```

| 字段             | 类型   | 必填 | 说明                                |
| ---------------- | ------ | ---- | ----------------------------------- |
| `id`             | string | 是   | 唯一标识                            |
| `sx`/`sy`/`sz`   | int>=1 | 是   | 内部尺寸                            |
| `payload`        | number |      | 装载承重上限（货物总重），null=不限 |
| `quantity_limit` | int>=1 |      | 可用数量上限，null=不限             |
| `obstacles`      | array  |      | 固定占位实体（见下）                |
| `facets`         | array  |      | 斜切平面禁区（见下）                |

### 障碍物 `obstacles`

容器内固定的轴对齐长方体占位（如门框包边、台阶、轮拱凸起），所有该类型实例共享。语义（禁入、顶面等价地板、计入体积率分母）见 [constraints.md](constraints.md) 1.11。

| 字段           | 类型   | 必填 | 说明                         |
| -------------- | ------ | ---- | ---------------------------- |
| `x`/`y`/`z`    | int>=0 | 是   | 容器内相对坐标（min corner） |
| `dx`/`dy`/`dz` | int>=1 | 是   | 沿容器轴的实际尺寸           |

预校验：障碍物必须完全在容器内、互不重叠；`existing_containers` 中已有放置不得与障碍物重叠。

### 斜面 `facets`

容器内斜切平面（如航空箱顶部斜切角），所有该类型实例共享。每个斜面用**恰好两个带符号轴名截距**描述，缺失的轴 = 斜面平行贯穿的轴（沿该轴截面不变）：

```json
"facets": [
  { "dx": 10, "dz": 10 },   // 平行 Y，X max 侧进 10、Z max 侧进 10 -> 顶前 45° 斜切
  { "dy": -8, "dz": 5 }     // 平行 X，Y min 侧进 8、Z max 侧进 5
]
```

| 字段           | 类型   | 必填     | 说明                                                       |
| -------------- | ------ | -------- | ---------------------------------------------------------- |
| `dx`/`dy`/`dz` | int!=0 | 恰好两个 | 沿该轴的截距：`+` = 从 max 侧向内进深，`-` = 从 min 侧向内 |

斜面切掉的角附近**楔形禁区**箱子不得侵入（面贴面允许）。语义（禁入、不参与支撑、计入体积率分母）详见 [constraints.md](constraints.md) 1.12；斜面之间允许重叠（独立禁区，重叠只是更禁）。

预校验：恰好两个非零截距、`1 <= |截距| <= 容器该轴尺寸`、`existing_containers` 已有放置不侵入斜面禁区。

## 箱子类型 `box_types`

```json
{
  "id": "box_l",
  "sx": 100,
  "sy": 50,
  "sz": 30,
  "allowed_orientations": ["xyz", "xzy"],
  "max_stack": 5,
  "max_load": [200.0, 150.0]
}
```

| 字段                   | 类型                     | 必填 | 说明                                      |
| ---------------------- | ------------------------ | ---- | ----------------------------------------- |
| `id`                   | string                   | 是   | 唯一标识                                  |
| `sx`/`sy`/`sz`         | int>=1                   | 是   | 原始尺寸（箱体自身坐标）                  |
| `allowed_orientations` | string[]                 | 是   | 允许朝向，枚举值见下                      |
| `max_stack`            | int>=1 或 int[]>=1       |      | 堆码层数上限，null=不限                   |
| `max_load`             | number>=0 或 number[]>=0 |      | 单箱上方承重上限，null=不限               |
| `weight`               | number>0                 |      | 箱型级重量（与箱子重量互斥，见下预校验）  |
| `loose`                | boolean                  |      | true=散件（先装托后装车），默认 false     |
| `group`                | string 或 null           |      | 箱型级分组 ID（与箱子级 group 三选一）    |
| `platform`             | string 或 null           |      | 箱型级站点 ID（与箱子级 platform 三选一） |

`group` 和 `platform` 分别采用严格三选一模式，每个字段独立选择来源：

- **全无**：所有箱型、箱子和已有放置都不配置该字段。
- **箱型级**：所有 `box_types` 都配置该字段，`boxes` 和已有放置都不配置；实例值按 `box_type_id` 继承。因此同一箱型不能跨不同分组或站点。
- **箱子级**：所有 `box_types` 都不配置该字段，每个 `boxes` 和已有放置都必须配置；不同实例可以有不同值。

箱型与箱子混用、部分配置、已有放置缺失配置均非法。`null` 等同未配置；实际配置值必须是非空字符串。两个字段可以选择不同来源，例如 `group` 使用箱型级而 `platform` 使用箱子级。

`max_stack` / `max_load` 为**承重约束**（机制与启用前提见 `docs/constraints.md` 1.8 / 1.9）：

- 标量：应用到全部朝向。
- 数组：长度必须等于 `allowed_orientations` 长度，按朝向分别取值（如平放堆 3 层、立放只能堆 2 层）。
- 任一箱型有非空值即启用对应约束（presence-based）。

**行业组合语义（朝上 / 易碎，无需专用字段）**：

- **朝上（this side up）**：`allowed_orientations: ["xyz", "yxz"]` —— 仅允许原 z 轴向上（可平面旋转，禁止侧放）。
- **易碎（fragile）**：`max_stack: 1` + `max_load: 0` —— 上方不可叠箱、不可承重。

朝向枚举，表示原始尺寸 `sx`/`sy`/`sz` 分别映射到容器坐标轴的 X/Y/Z：

- xyz: x->X, y->Y, z->Z
- yxz: x->Y, y->X, z->Z
- xzy: x->X, y->Z, z->Y
- zxy: x->Z, y->X, z->Y
- yzx: x->Y, y->Z, z->X
- zyx: x->Z, y->Y, z->X

## 箱子实例 `boxes`

```json
{
  "id": "b1",
  "box_type_id": "box_l",
  "weight": 10.0,
  "group": "A",
  "platform": "P1"
}
```

| 字段          | 类型   | 必填 | 说明                                                             |
| ------------- | ------ | ---- | ---------------------------------------------------------------- |
| `id`          | string | 是   | 唯一标识                                                         |
| `box_type_id` | string | 是   | 引用 box_types 中的 id                                           |
| `weight`      | number |      | 单箱重量，null=无重量                                            |
| `group`       | string |      | 箱子级分组 ID；仅在该字段选择箱子级模式时填写，用于 tender_limit |
| `platform`    | string |      | 箱子级站点 ID；仅在该字段选择箱子级模式时填写，用于路线约束      |

## 算法 `algorithm`（可选）

```json
"algorithm": "glc"
```

枚举值：`"gep"`（默认）、`"glc"`、`"rgs"`、`"bsg"`。

算法相关常量配置集中在 `src/core/algorithm/config.hpp`（编译期确定）。

目标为固定字典序的四维目标，不可配置，定义见 [architecture.md](architecture.md) §1.1。

## 约束 `constraints`（可选）

```json
{
  "time_limit": 120.0,
  "support_rate": 0.6,
  "platform_limit": null,
  "tender_limit": null
}
```

| 字段                  | 类型       | 默认  | 说明                                                                             |
| --------------------- | ---------- | ----- | -------------------------------------------------------------------------------- |
| `time_limit`          | number>0   | 120   | 时限（秒）                                                                       |
| `support_rate`        | number 0-1 | 0     | 底面支撑率阈值，0=跳过；装托模式下必须 > 0（预校验强制，否则托盘可在车厢内悬空） |
| `platform_limit`      | int>=1     | null  | 单容器最大站点数                                                                 |
| `tender_limit`        | int>=1     | null  | 每运输委托（tender）最多容器数（tender = 同 group 货物连通的容器连通分量）       |
| `pallet_fallback`     | boolean    | false | 散件装不进任何托盘时降级散装上车；false=未装箱（partial）                        |
| `pallet_support_rate` | number 0-1 | 1     | 托盘上箱子底面支撑率下限（装托专用，与装车 `support_rate` 独立）                 |
| `heavy_not_on_light`  | boolean    | false | 重不压轻：上箱重量不得超过直接支撑箱重量（需重量信息与 `support_rate > 0`）      |

堆码层数 `max_stack` 与单箱承重 `max_load` 不在此处配置，而是**箱型字段**（见上节），有值即启用。

## 托盘类型 `pallet_types`（可选）

启用装托（palletizing）：`loose: true` 的散件先装入托盘，托盘再作为装箱单元参与装车。任一存在即启用装托模式。装托流程与行为详见 [architecture.md](architecture.md) §3。

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
  ]
}
```

| 字段           | 类型   | 必填 | 说明                                       |
| -------------- | ------ | ---- | ------------------------------------------ |
| `id`           | string | 是   | 唯一标识                                   |
| `sx`/`sy`/`sz` | int>=1 | 是   | 托盘自身尺寸                               |
| `payload`      | number | 是   | 托盘装载承重上限（货物总重，不含托盘自重） |
| `max_height`   | int>=1 | 是   | 装载限高（货物堆高上限，不含托盘自身高度） |
| `self_weight`  | number |      | 托盘自重，默认 0                           |

- 装托模式要求有重量信息（箱型级或箱子级）、所有容器带 `payload`，否则 `invalid`。
- 散件箱型用 `box_types.loose: true` 标记；托盘内箱子底面支撑率用 `constraints.pallet_support_rate`。
- 散件装不进任何托盘：`constraints.pallet_fallback` 控制降级散装（true）或未装箱报错（false）。

## 路线 `route`（可选）

```json
["P0", "P1", "P2"]
```

按卸货顺序排列的站点 ID 列表（即配送顺序）：`route[0]` 先卸在近门处（X 大），末尾后卸在深处（X 小）。装货为反向：先装（最深处）的货最后卸，后装（近门处）的货最先卸。

## 预校验

Schema 校验后，代码还会检查：

- ID 唯一性：`container_types`、`box_types`、`boxes` 中各自的 `id` 必须唯一
- 引用完整性：每个 `box` 的 `box_type_id` 必须在 `box_types` 中存在
- 路线合法性：只要有箱子（含 `existing_containers` 中已有放置）的有效 `platform`，就必须提供 `route`；路线中无重复站点，箱子站点必须在路线中
- 重量一致性（**三选一**）：要么全无重量；要么**全部箱型**配置 `weight` 且**所有箱子不带**重量（箱子重量取箱型）；要么**所有箱子**配置 `weight` 且箱型不带。箱型与箱子重量混用、部分配置均报错。**有重量信息时**（后两种模式）要求全部容器配置 `payload`，全无重量时不要求；`max_load`/装托模式要求有重量信息（箱型级或箱子级）
- group/platform 一致性（**分别三选一**）：每个字段独立选择全无、全部箱型配置、或全部箱子（含已有放置）配置；箱型与箱子混用、部分配置均报错。箱型级值按 `box_type_id` 继承，箱子级值必须每个实例显式提供
- `tender_limit` 配置但无任何 `group` -> 报错（无 group 时该约束静默失效）
- 装托合法性：`pallet_types` id 唯一、`loose: true` 但未配置 `pallet_types` 报错；装托模式要求有重量信息（箱型级或箱子级）、全部容器带 `payload`、装车 `support_rate > 0`
- 障碍物合法性：每个障碍物必须完全在所属容器内、障碍物互不重叠、`existing_containers` 已有放置与障碍物不重叠
- 斜面合法性：每个斜面必须恰好两个非零截距、截距不越界、`existing_containers` 已有放置不侵入斜面禁区

## 中间状态 `existing_containers`（可选）

从已有部分放置继续装箱。`boxes` 只列**待装箱子**，已放置箱子信息完全由 `existing_containers` 描述。

```json
{
  "existing_containers": [
    {
      "type_id": "big",
      "placements": [
        {
          "box_id": "b0",
          "box_type_id": "box_l",
          "x": 0,
          "y": 0,
          "z": 0,
          "dx": 100,
          "dy": 50,
          "dz": 40,
          "orientation": "xyz",
          "weight": 10.0,
          "platform": "P1",
          "group": "A"
        }
      ]
    }
  ]
}
```

| 字段                       | 类型   | 必填 | 说明                                           |
| -------------------------- | ------ | ---- | ---------------------------------------------- |
| `type_id`                  | string | 是   | 引用 container_types 中的 id                   |
| `placements[].box_id`      | string | 是   | 箱子标识                                       |
| `placements[].box_type_id` | string | 是   | 引用 box_types 中的 id                         |
| `placements[].x/y/z`       | int>=0 | 是   | 放置位置（min corner）                         |
| `placements[].orientation` | string | 是   | 朝向，同 box_types 朝向枚举                    |
| `placements[].dx/dy/dz`    | int>=1 |      | 朝向后的实际尺寸（可从 type+朝向推导，可省略） |
| `placements[].weight`      | number |      | 箱子重量，未设置时为 null                      |
| `placements[].platform`    | string |      | 站点 ID（配送停靠点），未设置时为 null         |
| `placements[].group`       | string |      | 分组 ID，未设置时为 null                       |

`placements[].group` 和 `placements[].platform` 的填写遵循上面的三选一规则；字段类型为 `string|null`，箱型级模式下省略或填 `null`，由所属 `box_type_id` 继承有效值。

已有容器中的箱子不会出现在 `boxes` 列表中。求解器会先尝试在已有容器中继续塞入剩余箱子（未满则继续），再开新容器。已有放置被锁定，后处理不会移动它们。

预校验会额外检查已有放置的边界、重叠、重量、支撑率、站点限制和路线顺序。
