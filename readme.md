# pack3d

pack3d 是一个 C++20 三维装箱求解器，采用多种启发式算法求解 3D bin packing 问题。

pack3d 寓意将三维物体"打包"进容器空间，同时暗示将多种算法策略打包进统一求解框架。

## 架构概览

```
JSON Input -> Parser -> Solver (GLC / RGS / BSG) -> JSON Output
```

## 支持范围

支持的约束：

- 重量约束：容器内总重不超过容器限重
- 支撑率约束：箱子底面被支撑的比例
- 路线顺序约束：平台按装货顺序的 X 轴位置约束
- 平台数量限制：单容器最多 `platform_limit` 个不同平台
- 标段分散限制：同一 group 的箱子最多分散到 `tender_limit` 个容器

支持的算法：

- `GLC`（默认）：贪心前瞻构造 — 块装载 + 前瞻评估
- `RGS`：随机贪心搜索 — 多策略排序 + Shaw 随机化 + 多起点采样
- `BSG`：束搜索 — 宽度限制的启发式树搜索

目标字典序：`min_container_count` -> `min_platform_split` -> `max_volume_rate` -> `min_group_split`

支持的箱子朝向：XYZ、XZY、YXZ、YZX、ZXY、ZYX（6 种旋转），每种箱子类型可配置允许的朝向子集。

## 构建运行

### 环境要求

1. C++ 编译器，需要支持 C++20
2. XMake 3.0+
3. Python 3.9+（仅打包安装 Python SDK 时需要）

### 构建

第一次构建时会自动下载依赖库，请确保网络畅通。

构建所有可执行文件：

```bash
xmake build
```

### 运行

`xmake run cli <file> [-a <algorithm>] [-t <seconds>] [-s <rate>] [--platform-limit <n>] [--tender-limit <n>] [-o <dir>]`

- `file`：JSON 输入文件路径，必填
- `-a`：算法，可选 `glc` / `rgs` / `bsg`，默认 `glc`
- `-t`：时间限制（秒），默认 `120`
- `-s`：支撑率（0~1），默认 `0`
- `--platform-limit`：平台数量限制，默认不限制
- `--tender-limit`：标段数量限制，默认不限制
- `-o`：JSON 输出目录，默认 `output`

```bash
xmake run cli data/demo.json
xmake run cli data/demo.json -o result/ -a glc -t 30 -s 0.6
```

### Python SDK

可以作为 Python 包安装，供用户在 Python 环境中调用。

打包 Python SDK：

```bash
python -m build python
```

打包后会在 `python/dist/` 目录生成 `.whl` 和 `.tar.gz` 文件。
若在同一台机器上打包和安装，推荐 `.whl`；如果需要跨机器安装，推荐 `.tar.gz`：

```bash
pip install python/dist/xxx.whl
```

包名是 `pack3d`，提供 `pack3d.run(input) -> output` 函数：

- `input`：一个 dict，内容与 JSON 输入文件格式一致
- `output`：一个 dict，内容与 JSON 输出文件格式一致

可以直接运行脚本：

```bash
python run.py data/demo.json
```

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
python/
  README.md          Python SDK 说明
  pack3d/            Python SDK 入口
  setup.py           Python SDK 打包脚本
scripts/
  generate_data.py   BR 格式转 JSON
  draw.py            可视化输出
run.py               Python CLI 脚本
src/
  main.cpp            CLI 入口
  python_module.cpp   Python 绑定入口
  core/
    app.hpp/.cpp        统一入口
    solver.hpp/.cpp     算法路由
    types.hpp           共用类型
    constraints.hpp     约束函数
    objectives.hpp      目标向量
    tool.hpp            常用工具
    algorithm/
      config.hpp        编译期常量
      glc/              Greedy Lookahead Construction
      rgs/              Randomized Greedy Search
      bsg/              Beam Search Greedy
    postprocess.hpp/.cpp  共享后处理
tests/
  test_solver.cpp   求解器测试
  test_core.cpp     核心模块测试
  test_parser.cpp   解析测试
```

## 实现说明

完整的实现说明见 [docs/architecture.md](docs/architecture.md)，约束说明见 [docs/constraints.md](docs/constraints.md)。
