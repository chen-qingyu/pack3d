# pack3d Python SDK

pack3d 求解器的 Python 绑定，输入输出与 JSON 格式一致。

## 安装

```bash
python -m build python
```

构建产物位于 `python/dist/`。安装其中生成的 wheel：

```bash
pip install python/dist/pack3d-<version>-<tag>.whl
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

`run(input: dict) -> dict` 的输入与 JSON 格式一致。返回值恒含 `status`、`summary`、`result`、`violations`；`run()` 不抛异常，非法输入或内部错误返回 `status=invalid`。完整字段见 [docs/output.md](../docs/output.md)。

## 命令行脚本

```bash
python scripts/run.py data/demo.json
python scripts/run.py data/demo.json -a glc -t 30 -s 0.6
```
