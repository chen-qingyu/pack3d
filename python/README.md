# pack3d Python SDK

pack3d 求解器的 Python 绑定，输入输出与 JSON 格式一致。

## 安装

```bash
python -m build python
pip install python/dist/xxx.whl
```

若需跨机器安装，可使用 `.tar.gz`：

```bash
pip install python/dist/xxx.tar.gz
```

## 使用

```python
import pack3d

result = pack3d.run({
    "container_types": [...],
    "box_types": [...],
    "boxes": [...],
    "algorithm": "gep",
    "constraints": {
        "time_limit": 120.0,
        "support_rate": 0.6,
    },
})
```

| 参数    | 类型 | 说明                       |
| ------- | ---- | -------------------------- |
| `input` | dict | 输入数据，格式与 JSON 一致 |

返回值是一个 dict，顶层包含 `status`、`summary`、`result`、`violations` 四个字段（`violations` 恒存在，非 `complete` 时可能非空）。`run()` 不抛异常——非法输入或内部错误返回 `status=invalid` 的 dict，调用方按 `status` 判断成败。详见 [docs/output.md](../docs/output.md)。

## 命令行脚本

```bash
python run.py data/demo.json
python run.py data/demo.json -a glc -t 30 -s 0.6
```
