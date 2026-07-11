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

| 值           | 含义                          |
| ------------ | ----------------------------- |
| `"complete"` | 全部装箱完成                  |
| `"partial"`  | 算法完成，部分箱子未装入      |
| `"timeout"`  | 时限中断，返回当前最优        |
| `"blocked"`  | 硬约束阻断（如 tender_limit） |
| `"invalid"`  | 输入非法                      |

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
      "inner_size": { "x": 110, "y": 50, "z": 50 },
      "max_weight": 50000.0,
      "used_volume": 275000,
      "used_weight": 25.0,
      "volume_rate": 1.0,
      "weight_rate": 0.0005,
      "packed_count": 3,
      "platforms": ["A", "B"],
      "groups": ["X"],
      "placements": [
        {
          "box_id": "a1",
          "box_type_id": "big_box",
          "position": { "x": 0, "y": 0, "z": 0 },
          "size": { "dx": 50, "dy": 50, "dz": 50 },
          "orientation": "xyz",
          "platform": "P1",
          "group": "A"
        }
      ]
    }
  ],
  "box_types": [
    {
      "id": "big_box",
      "size": { "x": 50, "y": 50, "z": 50 },
      "allowed_orientations": ["xyz"]
    }
  ],
  "unpacked_boxes": ["b30", "b31"]
}
```

容器数组顺序即装车顺序。`null` 表示该维度不适用（如重量未配置时 `used_weight`/`weight_rate` 为 null；箱子未设置平台/分组时 `platform`/`group` 为 null）。

placement 字段说明：

| 字段          | 说明                              |
| ------------- | --------------------------------- |
| `box_id`      | 箱子实例 ID                       |
| `box_type_id` | 箱子类型 ID                       |
| `position`    | 放置位置（min corner）`{x, y, z}` |
| `size`        | 朝向后的实际尺寸 `{dx, dy, dz}`   |
| `orientation` | 朝向（`xyz`/`xzy`/...）           |
| `platform`    | 平台 ID，未设置时为 null          |
| `group`       | 分组 ID，未设置时为 null          |

## `violations` 校验违规（仅 `status: "invalid"` 时出现）

```json
["duplicate box id: b1", "unknown box_type_id 'foo' for box b2"]
```

字符串数组，每条为一个完整可读的违规描述。
