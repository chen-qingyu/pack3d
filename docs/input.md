# 输入格式

JSON，schema 见 `data/input_schema.json`。必填顶层字段：`container_types`、`box_types`、`boxes`。

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
  "max_weight": 50000.0,
  "quantity_limit": null
}
```

| 字段             | 类型   | 必填 | 说明                    |
| ---------------- | ------ | ---- | ----------------------- |
| `id`             | string | 是   | 唯一标识                |
| `sx`/`sy`/`sz`   | int>=1 | 是   | 内部尺寸                |
| `max_weight`     | number |      | 重量上限，null=不限     |
| `quantity_limit` | int>=1 |      | 可用数量上限，null=不限 |

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

| 字段                   | 类型                     | 必填 | 说明                        |
| ---------------------- | ------------------------ | ---- | --------------------------- |
| `id`                   | string                   | 是   | 唯一标识                    |
| `sx`/`sy`/`sz`         | int>=1                   | 是   | 原始尺寸（箱体自身坐标）    |
| `allowed_orientations` | string[]                 | 是   | 允许朝向，枚举值见下        |
| `max_stack`            | int>=1 或 int[]>=1       |      | 堆码层数上限，null=不限     |
| `max_load`             | number>=0 或 number[]>=0 |      | 单箱上方承重上限，null=不限 |

`max_stack` / `max_load` 为**承重约束**（详见 `docs/constraints.md` 1.8 / 1.9）：

- 标量：应用到全部朝向。
- 数组：长度必须等于 `allowed_orientations` 长度，按朝向分别取值（如平放堆 3 层、立放只能堆 2 层）。
- 任一箱型有非空值即启用对应约束（presence-based）；两者与 `support_rate` 相互独立，可同时开启。
- `max_load` 启用时要求所有箱子带重量。

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

| 字段          | 类型   | 必填 | 说明                       |
| ------------- | ------ | ---- | -------------------------- |
| `id`          | string | 是   | 唯一标识                   |
| `box_type_id` | string | 是   | 引用 box_types 中的 id     |
| `weight`      | number |      | 单箱重量，null=无重量      |
| `group`       | string |      | 分组 ID，用于 tender_limit |
| `platform`    | string |      | 平台 ID，用于路线约束      |

## 算法 `algorithm`（可选）

```json
"algorithm": "glc"
```

枚举值：`"gep"`（默认）、`"glc"`、`"rgs"`、`"bsg"`。

算法相关常量配置集中在 `src/core/algorithm/config.hpp`（编译期确定）。

目标为固定的四个维度，字典序：`min_container_count → min_platform_split → max_volume_rate → min_group_split`，不可配置。

## 约束 `constraints`（可选）

```json
{
  "time_limit": 120.0,
  "support_rate": 0.6,
  "platform_limit": null,
  "tender_limit": null
}
```

| 字段             | 类型       | 默认 | 说明                                                             |
| ---------------- | ---------- | ---- | ---------------------------------------------------------------- |
| `time_limit`     | number>0   | 120  | 时限（秒）                                                       |
| `support_rate`   | number 0-1 | 0    | 底面支撑率阈值，0=跳过                                           |
| `platform_limit` | int>=1     | null | 单容器最大平台数                                                 |
| `tender_limit`   | int>=1     | null | 每 tender 最多容器数（tender = 容器按共享 group 连通的连通分量） |

堆码层数 `max_stack` 与单箱承重 `max_load` 不在此处配置，而是**箱型字段**（见上节），有值即启用。

## 路线 `route`（可选）

```json
["P0", "P1", "P2"]
```

按装货先后排列的平台 ID 列表。先装平台在深处（X 小），后装平台在近门处（X 大）。

## 预校验

Schema 校验后，代码还会检查：

- ID 唯一性：`container_types`、`box_types`、`boxes` 中各自的 `id` 必须唯一
- 引用完整性：每个 `box` 的 `box_type_id` 必须在 `box_types` 中存在
- 路线合法性：只要有箱子（含 `existing_containers` 中已有放置）设置了 `platform`，就必须提供 `route`；路线中无重复平台，箱子平台必须在路线中
- 重量一致性：只要任一个箱子存在重量信息，则所有箱子和容器必须有重量信息

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
| `placements[].platform`    | string |      | 平台 ID，未设置时为 null                       |
| `placements[].group`       | string |      | 分组 ID，未设置时为 null                       |

已有容器中的箱子不会出现在 `boxes` 列表中。求解器会先尝试在已有容器中继续塞入剩余箱子（未满则继续），再开新容器。已有放置被锁定，后处理不会移动它们。

预校验会额外检查已有放置的边界、重叠、重量、支撑率、平台限制和路线顺序。
