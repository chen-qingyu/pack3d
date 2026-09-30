# pack3d

pack3d 是一个 C++20 三维装箱求解器，采用多种启发式算法求解 3D bin packing 问题。

## 架构概览

```
JSON Input -> Parser -> Solver (GEP / GLC / RGS / BSG) -> Post-process -> JSON Output
```

## 支持范围

核心能力包括重量、支撑、路线、站点、运输委托、堆码、障碍物、斜面、危险品分柜、装托和续装。字段与约束定义见 [docs/input.md](docs/input.md) 和 [docs/constraints.md](docs/constraints.md)。

支持的算法：

- `GEP`（默认）：极点贪心法 — 体积降序 + EP 优先填充
- `GLC`：贪心前瞻构造 — 块装载 + beam 搜索
- `RGS`：随机贪心搜索 — 多策略排序 + Shaw 随机化 + 多起点采样
- `BSG`：束搜索 — 宽度限制的启发式树搜索 + KPA 块合并

四种算法复用同一约束层；具体策略与适用场景见 [docs/algorithms.md](docs/algorithms.md)。

目标字典序：`min_container_count -> min_platform_split -> max_volume_rate -> min_group_split`

支持六种箱子朝向，每种箱子类型可配置允许的朝向子集。

## 构建运行

### 环境要求

1. C++ 编译器，需要支持 [C++20](https://en.cppreference.com/cpp/20) 标准
2. [XMake](https://github.com/xmake-io/xmake) 3.0+

### 构建

第一次构建时会自动下载依赖库，请确保网络畅通。

```bash
xmake build
```

可用 target：`cli`（命令行）、`lib`（Python 模块）、`test`（测试）、`report`（benchmark 报告）。

### 运行

提供四种使用方式。

#### CLI

通过命令行直接运行求解器。

```bash
xmake run cli data/demo.json -a gep -t 30 -s 0.6
```

输入文件路径必填；`-o` 输出目录默认 `output/`；`-a` 默认 `gep`；`-t`/`-s` 不传时用输入默认（120 秒 / 0）；另支持 `--platform-limit`、`--tender-limit` 覆盖 JSON 中的约束值（与 JSON 显式指定冲突时报错）。

#### SDK

需要 Python 3.9+。作为 Python 包供上层代码调用，也提供命令行脚本：

```python
import pack3d
result = pack3d.run({...})   # 输入为与 JSON 同构的 dict
```

```bash
python scripts/run.py data/demo.json
```

安装与完整用法见 [python/README.md](python/README.md)

#### API

通过 HTTP 服务远程调用求解器，提供实例/运行管理、结果下载等 14 个端点。

```bash
pip install fastapi uvicorn
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

详见 [docs/api.md](docs/api.md)

#### Web

浏览器工作台，通过 HTTP API 管理实例和运行，并在浏览器中三维查看装箱结果。前端代码在 `web/`。

```bash
# 终端 1：启动后端
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000

# 终端 2：启动前端（开发模式）
cd web && npm install && npm run dev
```

详见 [docs/web.md](docs/web.md)

## 输入输出

### 输入

输入文件为 JSON 格式，包含容器类型、箱子类型、箱子列表及约束参数等信息。

输入的详细定义见 [docs/input.md](docs/input.md)。

### 输出

输出为一个 JSON 文件，包含每个容器的装载方案、箱子放置位置与朝向等信息。

输出的详细定义见 [docs/output.md](docs/output.md)。

## 演示输入

[demo.json](data/demo.json) 是可直接运行的完整示例，包含两种容器、两种箱子和五个待装实例。

## 文档索引

| 主题           | 位置                                                           |
| -------------- | -------------------------------------------------------------- |
| 整体架构与流程 | [docs/architecture.md](docs/architecture.md)                   |
| 算法细节       | [docs/algorithms.md](docs/algorithms.md)                       |
| 输入格式       | [docs/input.md](docs/input.md)                                 |
| 输出格式       | [docs/output.md](docs/output.md)                               |
| 约束条件       | [docs/constraints.md](docs/constraints.md)                     |
| HTTP API       | [docs/api.md](docs/api.md)                                     |
| Web 工作台     | [docs/web.md](docs/web.md)                                     |
| Python SDK     | [python/README.md](python/README.md)                           |
| 编译期配置常量 | [src/core/algorithm/config.hpp](src/core/algorithm/config.hpp) |
| benchmark 报告 | [report/report.txt](report/report.txt)                         |

## 目录结构

```text
data/
  br-origin/         BR 原始数据
  tests/             测试数据
  demo.json          示例输入
  input_schema.json  输入 JSON Schema
docs/                文档说明
python/              Python SDK 相关
report/              benchmark 报告程序和结果
scripts/
  draw.py            可视化绘图脚本
  generate_data.py   BR 原始数据转 JSON 脚本
  run.py             Python SDK 命令行脚本
server/              API 相关
src/
  main.cpp           CLI 入口
  python_module.cpp  Python 绑定源码
  core/              数据结构与核心算法
tests/               单元测试与集成测试
web/                 前端工作台
```
