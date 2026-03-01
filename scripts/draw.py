"""
绘制 3D 可交互装箱图。
Usage:
    py draw.py <result_json>
"""

import sys
import json

import numpy as np
import plotly.graph_objects as go
import plotly.subplots
import plotly.express.colors


def main(data: dict):
    """绘制3D装箱图主函数"""
    containers = data["containers"]
    box_types = {bt["id"]: bt for bt in data["box_types"]}

    # 计算绘图相关参数
    max_dims = calc_max_dims(containers)
    n = len(containers)
    cols = min(4, n)  # 最多4列
    rows = (n + cols - 1) // cols

    # 创建子图
    infos = []
    for c in containers:
        info = f"Container {c['type']['id']}<br>"
        info += f"<sub>Volume Rate: {c['volume_rate']:.2%}</sub>"
        if "weight_rate" in c:
            info += f"<sub>, Weight Rate: {c['weight_rate']:.2%}</sub>"
        infos.append(info)
    subplot_titles = infos
    fig = plotly.subplots.make_subplots(
        rows=rows, cols=cols,
        specs=[[{"type": "scatter3d"} for _ in range(cols)] for _ in range(rows)],
        subplot_titles=subplot_titles,
    )

    # 绘制每个装箱方案
    for i, container in enumerate(containers):
        row = i // cols + 1
        col = i % cols + 1
        draw(container, fig, row, col, max_dims, box_types)

    # 添加视图切换功能
    add_view_selector(fig, rows, cols)

    # 显示图形
    fig.show()


def draw(container: dict, fig: go.Figure, row: int, col: int, max_dims: tuple[int, int, int], box_types: dict, shown_types=set()):
    """绘制容器和箱子"""
    # 绘制容器
    draw_container(fig, container, row, col)

    # 绘制箱子
    for box in container["boxes"]:
        draw_box(fig, box, row, col, shown_types, box_types)

    # 设置场景
    max_dim = max(max_dims)
    scene_config = dict(
        xaxis=dict(range=[0, max_dims[0]], title="X"),
        yaxis=dict(range=[0, max_dims[1]], title="Y"),
        zaxis=dict(range=[0, max_dims[2]], title="Z"),
        aspectratio=dict(
            x=max_dims[0] / max_dim,
            y=max_dims[1] / max_dim,
            z=max_dims[2] / max_dim
        )
    )
    scene = f"scene{(row-1)*4 + col}" if (row-1)*4 + col > 1 else "scene"
    fig.layout[scene].update(scene_config)


def draw_container(fig: go.Figure, container: dict, row: int, col: int):
    """绘制容器"""
    t = container["type"]
    l, w, h = t["lx"], t["ly"], t["lz"]

    vertices = np.array([
        [0, 0, 0], [l, 0, 0], [l, w, 0], [0, w, 0],  # 底面4个顶点
        [0, 0, h], [l, 0, h], [l, w, h], [0, w, h]   # 顶面4个顶点
    ])
    line = dict(color='gray', width=2)
    draw_edges(fig, vertices, line, row, col)


def draw_box(fig: go.Figure, box: dict, row: int, col: int, shown_types: set, box_types: dict):
    """绘制箱子"""
    x, y, z = box["x"], box["y"], box["z"]
    box_type = box_types[box["type"]]
    l, w, h = get_oriented_dim(box_type["lx"], box_type["ly"], box_type["lz"], box["orient"])
    color = get_color(box["type"])

    # 定义长方体的8个顶点
    vertices = np.array([
        [x, y, z], [x + l, y, z], [x + l, y + w, z], [x, y + w, z],
        [x, y, z + h], [x + l, y, z + h], [x + l, y + w, z + h], [x, y + w, z + h],
    ])

    # 定义6个面的顶点索引（每个面由2个三角形组成）
    faces = [
        # 底面 (z=0)
        [0, 1, 2], [0, 2, 3],
        # 顶面 (z=h)
        [4, 5, 6], [4, 6, 7],
        # 前面 (y=0)
        [0, 1, 5], [0, 5, 4],
        # 后面 (y=w)
        [3, 2, 6], [3, 6, 7],
        # 左面 (x=0)
        [0, 3, 7], [0, 7, 4],
        # 右面 (x=l)
        [1, 2, 6], [1, 6, 5]
    ]

    fig.add_trace(
        go.Mesh3d(
            x=vertices[:, 0],
            y=vertices[:, 1],
            z=vertices[:, 2],
            i=[face[0] for face in faces],
            j=[face[1] for face in faces],
            k=[face[2] for face in faces],
            color=color,
            name=box["type"],
            legendgroup=box["type"],
            showlegend=box["type"] not in shown_types,
            text=get_text(box, box_type),
            hoverinfo='text',
        ),
        row=row, col=col
    )
    shown_types.add(box["type"])

    line = dict(color='black', width=1)
    draw_edges(fig, vertices, line, row, col)


def draw_edges(fig: go.Figure, vertices: np.ndarray, line: dict, row: int, col: int):
    """绘制长方体边框"""
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),  # 底面
        (4, 5), (5, 6), (6, 7), (7, 4),  # 顶面
        (0, 4), (1, 5), (2, 6), (3, 7)   # 竖直边
    ]
    for a, b in edges:
        fig.add_trace(
            go.Scatter3d(
                x=[vertices[a, 0], vertices[b, 0]],
                y=[vertices[a, 1], vertices[b, 1]],
                z=[vertices[a, 2], vertices[b, 2]],
                mode='lines',
                line=line,
                showlegend=False,
                hoverinfo='skip',
            ),
            row=row, col=col
        )


def get_text(box: dict, box_type: dict) -> str:
    """生成用于鼠标悬浮显示的文本信息"""
    text = f"box: {box["id"]}<br>"
    text += f"type: {box["type"]}<br>"
    text += f"size: ({box_type["lx"]}x{box_type["ly"]}x{box_type["lz"]})<br>"
    text += f"pos: ({box["x"]}, {box["y"]}, {box["z"]})<br>"
    text += f"orient: {box["orient"]}<br>"
    return text


def get_oriented_dim(l: int, w: int, h: int, orient: str) -> tuple[int, int, int]:
    """根据orient获取实际放置的尺寸"""
    orient_map = {
        "XYZ": (l, w, h),
        "YXZ": (w, l, h),
        "XZY": (l, h, w),
        "ZXY": (h, l, w),
        "YZX": (w, h, l),
        "ZYX": (h, w, l),
    }
    return orient_map[orient]


def get_color(type_id: str, colors={}):
    """一种箱型一个颜色"""
    if type_id not in colors:
        base = plotly.express.colors.qualitative.Plotly
        colors[type_id] = base[len(colors) % len(base)]
    return colors[type_id]


def calc_max_dims(containers: list[dict]) -> tuple[int, int, int]:
    """计算所有容器在长宽高三个维度的最大尺寸"""
    max_l, max_w, max_h = 0, 0, 0
    for container in containers:
        max_l = max(max_l, container["type"]["lx"])
        max_w = max(max_w, container["type"]["ly"])
        max_h = max(max_h, container["type"]["lz"])
    return (max_l, max_w, max_h)


def add_view_selector(fig: go.Figure, rows: int, cols: int):
    """添加视图选择器"""

    # 定义标准视图的相机参数
    views = {
        "Default": None,  # None表示使用plotly默认相机
        "Front": {"eye": {"x": 0, "y": -2, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "Back": {"eye": {"x": 0, "y": 2, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "Left": {"eye": {"x": -2, "y": 0, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "Right": {"eye": {"x": 2, "y": 0, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "Top": {"eye": {"x": 0, "y": 0, "z": 2}, "up": {"x": 0, "y": 1, "z": 0}},
        "Bottom": {"eye": {"x": 0, "y": 0, "z": -2}, "up": {"x": 0, "y": -1, "z": 0}},
    }

    # 更新所有子图的相机
    scenes = ["scene"] + [f"scene{i}" for i in range(2, rows * cols + 1)]
    buttons = []
    for view_name, camera in views.items():
        buttons.append(
            dict(
                label=view_name,
                method="relayout",
                args=[{f"{scene}.camera": camera for scene in scenes}],
            )
        )

    # 添加下拉菜单
    fig.update_layout(
        updatemenus=[
            dict(
                buttons=buttons,
                direction="down",
                showactive=True,
                active=0,
                xanchor="left",
                yanchor="top",
            )
        ]
    )


if __name__ == "__main__":
    # 检查命令行参数
    if len(sys.argv) != 2:
        print("Usage: py draw.py <result_json>")
        sys.exit(1)
    # 加载装箱方案数据
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        data = json.load(f)
    # 绘制3D装箱图
    main(data)
