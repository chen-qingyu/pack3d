# API 文档

## 概述

pack3d-api 是一个 RESTful HTTP 服务，提供三维装箱求解的多实例管理。

- **Instance**：命名的项目身份，可多次运行（每次运行输入/参数可不同）
- **Run**：一次求解执行，产生 JSON 结果

所有请求/响应为 `application/json`，结果下载为 `application/json`。

## 端点总览

| 方法     | 路径                                             | 说明                |
| -------- | ------------------------------------------------ | ------------------- |
| `POST`   | `/api/instances`                                 | 创建实例            |
| `GET`    | `/api/instances`                                 | 实例列表            |
| `GET`    | `/api/instances/{id}`                            | 实例详情（含 runs） |
| `PATCH`  | `/api/instances/{id}`                            | 重命名实例          |
| `DELETE` | `/api/instances/{id}`                            | 删实例 + 全部 runs  |
| `POST`   | `/api/instances/{id}/runs`                       | 创建运行            |
| `GET`    | `/api/instances/{id}/runs`                       | 运行列表            |
| `GET`    | `/api/instances/{id}/runs/{rid}`                 | 运行状态            |
| `PATCH`  | `/api/instances/{id}/runs/{rid}`                 | 重命名运行          |
| `GET`    | `/api/instances/{id}/runs/{rid}/result`          | 获取结果 JSON       |
| `GET`    | `/api/instances/{id}/runs/{rid}/result/download` | 下载结果文件        |
| `GET`    | `/api/instances/{id}/runs/{rid}/input`           | 查看输入 JSON       |
| `POST`   | `/api/instances/{id}/runs/{rid}/cancel`          | 终止运行            |
| `DELETE` | `/api/instances/{id}/runs/{rid}`                 | 删除运行            |

## Instance 端点

### `POST /api/instances`

**请求：**

```json
{ "name": "仓库装箱" }
```

| 字段   | 类型           | 必填 | 说明                       |
| ------ | -------------- | ---- | -------------------------- |
| `name` | string (1-128) | 是   | 实例名称，路径字符自动消毒 |

**响应 `201`：**

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "仓库装箱",
  "created_at": "2026-07-11T19:42:46+00:00",
  "run_count": 0
}
```

### `GET /api/instances`

```json
{
  "instances": [
    {
      "instance_id": "a1b2c3d4e5f6",
      "instance_name": "仓库装箱",
      "created_at": "...",
      "run_count": 3
    }
  ]
}
```

### `GET /api/instances/{instance_id}`

返回实例详情 + 所有 runs（按时间倒序）：

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "仓库装箱",
  "created_at": "...",
  "run_count": 2,
  "runs": [
    {
      "run_id": "r1r2r3r4r5r6",
      "instance_id": "a1b2c3d4e5f6",
      "instance_name": "仓库装箱",
      "run_name": "仓库装箱",
      "status": "completed",
      "error": null,
      "created_at": "...",
      "summary": {
        "elapsed_second": 0.75,
        "packed_box_count": 5,
        "unpacked_box_count": 0,
        "container_count": 2,
        "platform_split": 0,
        "volume_rate": 0.468,
        "group_split": 0,
        "pallet_count": 0,
        "palletized_box_count": 0,
        "loose_box_count": 0
      }
    }
  ]
}
```

### `PATCH /api/instances/{instance_id}`

```json
{ "name": "新名称" }
```

### `DELETE /api/instances/{instance_id}`

级联删除该实例下所有 runs 及文件。

## Run 端点

### `POST /api/instances/{instance_id}/runs`

**请求：**

```json
{
  "input_json": {
    "container_types": [...],
    "box_types": [...],
    "boxes": [...],
    "algorithm": "gep",
    "constraints": { "time_limit": 30, "support_rate": 0.6 }
  },
  "run_name": "仓库装箱"
}
```

| 字段         | 类型   | 必填 | 默认值          | 说明             |
| ------------ | ------ | ---- | --------------- | ---------------- |
| `input_json` | object | 是   | —               | pack3d 输入 JSON |
| `run_name`   | string | 否   | `instance_name` | 运行名称         |

**响应 `201`：**

```json
{
  "run_id": "r1r2r3r4r5r6",
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "仓库装箱",
  "run_name": "仓库装箱",
  "status": "running",
  "error": null,
  "created_at": "..."
}
```

### `GET /api/instances/{instance_id}/runs`

返回该实例下所有 runs，按时间倒序：

```json
{
  "runs": [
    {
      "run_id": "...",
      "instance_id": "...",
      "instance_name": "...",
      "run_name": "...",
      "status": "completed",
      "error": null,
      "created_at": "...",
      "summary": { ... }
    }
  ]
}
```

### `GET /api/instances/{instance_id}/runs/{run_id}`

返回单个 run 状态：

| status      | 说明                                |
| ----------- | ----------------------------------- |
| `running`   | 求解执行中，`summary` 为 null       |
| `completed` | 正常完成，`summary` 包含装箱结果    |
| `invalid`   | 输入非法，`violations` 包含错误详情 |
| `failed`    | 异常中断（如服务重启）              |
| `cancelled` | 用户终止                            |

### `GET /api/instances/{instance_id}/runs/{run_id}/result`

返回完整求解结果 JSON，包含 `status`、`summary`、`result`（含容器装载详情）。

仅 `completed` 或 `invalid` 状态可获取，否则返回 `409`。

### `GET /api/instances/{instance_id}/runs/{run_id}/result/download`

下载结果 JSON 文件（`application/json`，`Content-Disposition: attachment`）。

文件名格式：`{run_name}-output.json`。

### `GET /api/instances/{instance_id}/runs/{run_id}/input`

返回本次 run 的输入 JSON（创建 run 时写入，始终可获取）。

### `PATCH /api/instances/{instance_id}/runs/{run_id}`

```json
{ "name": "新运行名" }
```

### `POST /api/instances/{instance_id}/runs/{run_id}/cancel`

终止正在运行的求解。仅 `running` 状态可取消，否则返回 `409`。

### `DELETE /api/instances/{instance_id}/runs/{run_id}`

删除运行及其所有文件。

## 启动

```bash
py -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

## 测试示例

```bash
# 创建实例
xh :8000/api/instances name=demo

# 提交求解
xh :8000/api/instances/{id}/runs input_json:=@data/demo.json

# 查看结果
xh :8000/api/instances/{id}/runs/{rid}

# 获取结果
xh :8000/api/instances/{id}/runs/{rid}/result
```
