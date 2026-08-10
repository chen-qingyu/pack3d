# pack3d

pack3d 是一个 C++20 三维装箱求解器，采用多种启发式算法求解 3D bin packing 问题。

pack3d 寓意将三维物体"打包"进容器空间，同时暗示将多种算法策略打包进统一求解框架。

## 架构概览

```
JSON Input -> Parser -> Solver (GEP / GLC / RGS / BSG) -> Post-process -> JSON Output
```

## 支持范围

支持的功能：

- 重量约束：容器内总重不超过容器限重
- 支撑率约束：箱子底面被支撑的比例
- 路线顺序约束：站点（配送停靠点）按装卸货通道的 X 轴位置约束
- 站点数量限制：单容器最多 `platform_limit` 个不同站点
- 运输委托限制：每个 tender（运输委托 = 同 group 货物连通的容器连通分量）最多包含 `tender_limit` 个容器
- 堆码层数约束：箱型 `max_stack` 限制堆叠层数（标量或按朝向数组）
- 单箱承重约束：箱型 `max_load` 限制单箱上方承重（标量或按朝向数组）
- 障碍物约束：容器内轴对齐障碍物，箱体不得侵入（面贴面允许），障碍物顶面等价地板
- 斜面约束：容器斜面楔形禁入区，箱体不得侵入
- 装托（palletizing）：`palletize` 箱型散件先装托，托盘作为装箱单元装车（详见 [docs/palletizing.md](docs/palletizing.md)）
- 中间状态续装：从已有部分放置继续装箱（详见 [docs/resume.md](docs/resume.md)）

支持的算法：

- `GEP`（默认）：极点贪心法 — 体积降序 + EP 优先填充
- `GLC`：贪心前瞻构造 — 块装载 + beam 搜索
- `RGS`：随机贪心搜索 — 多策略排序 + Shaw 随机化 + 多起点采样
- `BSG`：束搜索 — 宽度限制的启发式树搜索 + KPA 块合并

每种算法均支持全部的功能，且可通过配置启用/禁用部分约束。

目标字典序：`min_container_count -> min_platform_split -> max_volume_rate -> min_group_split`

支持的箱子朝向：XYZ、XZY、YXZ、YZX、ZXY、ZYX（6 种旋转），每种箱子类型可配置允许的朝向子集。

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

输入文件路径必填；`-o` 输出目录默认 `output/`；`-a` 默认 `gep`；`-t` 默认 `120`；`-s` 默认 `0`；另支持 `--platform-limit`、`--tender-limit` 覆盖 JSON 中的约束值（与 JSON 显式指定冲突时报错）。

#### SDK

需要 Python 3.9+。作为 Python 包供上层代码调用。

```bash
pip install python/dist/xxx.whl
```

```python
import pack3d
result = pack3d.run({"container_types": [...], "box_types": [...], "boxes": [...]})
```

也提供命令行脚本：`python run.py data/demo.json`

详见 [python/README.md](python/README.md)

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

## 最小示例

仓库里的 [demo.json](data/demo.json) 是一个可直接运行的完整例子。该例子定义 2 种容器（small 100×100×100、large 200×100×100）、2 种箱子（box_s 30×20×20、box_l 100×100×90），共 5 个箱子。约束为时间限制 30 秒、支撑率 0.6。

## 文档索引

| 主题           | 位置                            |
| -------------- | ------------------------------- |
| 整体架构与流程 | `docs/architecture.md`          |
| 输入格式       | `docs/input.md`                 |
| 输出格式       | `docs/output.md`                |
| 约束条件       | `docs/constraints.md`           |
| BSG 算法细节   | `docs/bsg.md`                   |
| 装托           | `docs/palletizing.md`           |
| 中间状态续装   | `docs/resume.md`                |
| HTTP API       | `docs/api.md`                   |
| Web 工作台     | `docs/web.md`                   |
| Python SDK     | `python/README.md`              |
| 编译期配置常量 | `src/core/algorithm/config.hpp` |

## 代码结构

```text
data/
  demo.json          示例 JSON
  input_schema.json  输入 JSON Schema（构建时嵌入为 C++ 头文件）
  convert_br.py      BR 格式 benchmark 转 JSON
  br-origin/         BR 格式 benchmark 数据
  tests/             测试数据（含续装、承重、站点合并等专项场景）
docs/                文档说明
python/
  README.md          Python SDK 说明
  pack3d/            Python SDK 入口
  setup.py           Python SDK 打包脚本
server/
  main.py            FastAPI 端点
  manager.py         实例/运行生命周期
  db.py              SQLite 持久化
web/
  index.html, ...    前端入口
  src/               前端源码（App、组件、Viewer3D、hooks）
report/
  report.cpp         benchmark 报告程序
  report.txt         benchmark 报告输出
run.py               Python CLI 脚本
src/
  main.cpp           CLI 入口
  python_module.cpp  Python 绑定入口
  core/
    app.hpp/.cpp              统一入口
    packer_base.hpp/.cpp      多态基类
    types.hpp                 共用类型
    constraints.hpp/.cpp      约束模块
    objectives.hpp/.cpp       目标向量
    postprocess.hpp/.cpp      后处理
    pallet.hpp/.cpp           装托类型与虚拟容器/箱型
    palletizer.hpp/.cpp       装托流水线（散件→托盘）
    io.hpp/.cpp               输入输出
    select_container.hpp/.cpp 选车模块
    tool.hpp/.cpp             常用工具
    algorithm/
      config.hpp              编译期常量
      gep/                    Greedy Extreme Point
      glc/                    Greedy Lookahead Construction
      rgs/                    Randomized Greedy Search
      bsg/                    Beam Search Greedy
tests/               单元测试和集成测试
```
