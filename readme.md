# pack3d

pack3d 是一个 C++20 三维装箱求解器，采用多种启发式算法求解 3D bin packing 问题。

pack3d 寓意将三维物体"打包"进容器空间，同时暗示将多种算法策略打包进统一求解框架。

## 架构概览

```
JSON Input -> Parser -> Solver (GEP / GLC / RGS / BSG) -> Post-process -> JSON Output
```

## 支持范围

支持的约束：

- 重量约束：容器内总重不超过容器限重
- 支撑率约束：箱子底面被支撑的比例
- 路线顺序约束：平台按装货顺序的 X 轴位置约束
- 平台数量限制：单容器最多 `platform_limit` 个不同平台
- 标段分散限制：同一 group 的箱子最多分散到 `tender_limit` 个容器

支持的算法：

- `GEP`（默认）：极点贪心法 — 体积降序 + EP 优先填充
- `GLC`：贪心前瞻构造 — 块装载 + beam 搜索
- `RGS`：随机贪心搜索 — 多策略排序 + Shaw 随机化 + 多起点采样
- `BSG`：束搜索 — 宽度限制的启发式树搜索 + KPA 块合并

目标字典序：`min_container_count -> min_platform_split -> max_volume_rate -> min_group_split`

支持的箱子朝向：XYZ、XZY、YXZ、YZX、ZXY、ZYX（6 种旋转），每种箱子类型可配置允许的朝向子集。

## 构建运行

### 环境要求

1. C++ 编译器，需要支持 [C++20](https://en.cppreference.com/cpp/20) 标准
2. [XMake](https://github.com/xmake-io/xmake) 3.0+

### 构建

第一次构建时会自动下载依赖库，请确保网络畅通。

构建所有可执行文件：

```bash
xmake build
```

### 运行

提供三种使用方式。

#### CLI

通过命令行直接运行求解器。

```bash
xmake run cli data/demo.json -a gep -t 30 -s 0.6
```

输入文件路径必填；`-a` 默认 `gep`；`-t` 默认 `120`；`-s` 默认 `0`。

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

通过 HTTP 服务远程调用求解器。

```bash
pip install fastapi uvicorn
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

详见 [docs/api.md](docs/api.md)

## 输入输出

### 输入

输入文件为 JSON 格式，包含容器类型、箱子类型、箱子列表及约束参数等信息。

输入的详细定义见 [docs/input.md](docs/input.md)。

### 输出

输出为一个 JSON 文件，包含每个容器的装载方案、箱子放置位置与朝向等信息。

输出的详细定义见 [docs/output.md](docs/output.md)。

## 最小示例

仓库里的 [demo.json](data/demo.json) 是一个可直接运行的完整例子。该例子定义 2 种容器（small 100×100×100、large 200×100×100）、2 种箱子（box_s 30×20×20、box_l 100×100×90），共 5 个箱子。约束为时间限制 30 秒、支撑率 0.6。

## 代码结构

```text
data/
  demo.json          示例 JSON
  input_schema.json  输入 JSON Schema
  br-origin/         BR 格式 benchmark 数据
  tests/             测试数据
docs/
  architecture.md    求解器架构
  constraints.md     约束条件
  input.md           输入格式
  output.md          输出格式
  api.md             HTTP API 文档
python/
  README.md          Python SDK 说明
  pack3d/            Python SDK 入口
  setup.py           Python SDK 打包脚本
server/
  main.py            FastAPI 端点
  manager.py         实例/运行生命周期
  db.py              SQLite 持久化
scripts/
  generate_data.py   BR 格式转 JSON
  draw.py            可视化输出
run.py               Python CLI 脚本
src/
  main.cpp            CLI 入口
  python_module.cpp   Python 绑定入口
  core/
    app.hpp/.cpp        统一入口
    packer_base.hpp     PackerBase 多态基类
    solver.hpp/.cpp     算法路由
    types.hpp           共用类型
    constraints.hpp     约束函数
    objectives.hpp      目标向量
    postprocess.hpp     共享后处理
    tool.hpp            常用工具
    algorithm/
      config.hpp        编译期常量
      gep/              Greedy Extreme Point
      glc/              Greedy Lookahead Construction
      rgs/              Randomized Greedy Search
      bsg/              Beam Search Greedy
tests/
  test_solver.cpp   求解器测试
  test_core.cpp     核心模块测试
  test_parser.cpp   解析测试
  test_bsg.cpp      BSG 测试
```

## 实现说明

完整的实现说明见 [docs/architecture.md](docs/architecture.md)，约束说明见 [docs/constraints.md](docs/constraints.md)。
