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
  "group_split": 0
}
```

| 字段                 | 说明                                       |
| -------------------- | ------------------------------------------ |
| `elapsed_second`     | 耗时（秒）                                 |
| `packed_box_count`   | 已装箱数                                   |
| `unpacked_box_count` | 未装箱数                                   |
| `container_count`    | 使用容器数                                 |
| `platform_split`     | 平台拆分总次数（0=每个平台只在一个容器中） |
| `volume_rate`        | 各容器平均体积利用率（0-1）                |
| `group_split`        | 组拆分总次数（0=每组只在一个容器中）       |

## `result` 装箱结果

```json
{
  "containers": [
    {
      "type_id": "big",
      "sx": 110,
      "sy": 50,
      "sz": 50,
      "max_weight": 50000.0,
      "used_volume": 275000,
      "used_weight": 25.0,
      "volume_rate": 1.0,
      "weight_rate": 0.0005,
      "packed_count": 3,
      "platforms": ["A", "B"],
      "groups": ["X"],
      "tender": 1,
      "obstacles": [{ "x": 0, "y": 0, "z": 40, "dx": 3, "dy": 50, "dz": 10 }],
      "facets": [{ "dx": 10, "dz": 10 }],
      "placements": [
        {
          "box_id": "a1",
          "box_type_id": "big_box",
          "x": 0,
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
  "box_types": [
    {
      "id": "big_box",
      "sx": 50,
      "sy": 50,
      "sz": 50,
      "allowed_orientations": ["xyz"],
      "max_stack": [3],
      "max_load": [200.0]
    }
  ],
  "unpacked_boxes": ["b30", "b31"]
}
```

容器数组顺序即装车顺序。`null` 表示该维度不适用（如重量未配置时 `used_weight`/`weight_rate` 为 null；箱子未设置平台/分组时 `platform`/`group` 为 null）。

`obstacles` 为本容器实例的障碍物（从容器类型继承，自包含），结构与输入一致；容器类型未配置障碍物时省略。

`facets` 为本容器实例的斜面（从容器类型继承，自包含），结构与输入一致（`dx`/`dy`/`dz` 恰好两个）；容器类型未配置斜面时省略。

`tender` 为该容器所属 tender 的序号（1-based）：容器按共享 `group` 连通，每个连通分量即一个 tender，按容器顺序首次出现编号。无 group 的容器为 `null`。如容器 A{g1,g2}、B{g2,g3}、C{g3,g4}、D{g5}，则 A/B/C 的 `tender` 均为 1，D 为 2。

`result.box_types` 与输入 `box_types` 结构一致，并回显输入中配置的 `max_stack` / `max_load`（与 `allowed_orientations` 对齐的数组，未配置则省略）。

placement 字段说明：

| 字段           | 说明                           |
| -------------- | ------------------------------ |
| `box_id`       | 箱子实例 ID                    |
| `box_type_id`  | 箱子类型 ID                    |
| `x`/`y`/`z`    | 放置位置（min corner）         |
| `dx`/`dy`/`dz` | 朝向后的实际尺寸（沿容器轴向） |
| `orientation`  | 朝向（`xyz`/`xzy`/...）        |
| `weight`       | 箱子重量，未设置时为 null      |
| `platform`     | 平台 ID，未设置时为 null       |
| `group`        | 分组 ID，未设置时为 null       |

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
