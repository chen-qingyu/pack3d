#! python3

"""
绘制 3D 装箱图。

主要功能:
• 从 JSON 文件读取以可交互图形绘制并保存为 HTML 文件
• 鼠标悬浮显示箱子的基本信息
• 支持标准视图切换（前、后、左、右、俯、仰）
• 支持三种上色模式：按箱型 / 按平台 / 按分组
• 支持按容器筛选显示，避免多容器时视图拥挤
• 自动选择差异度最高的维度作为默认上色模式

使用说明:
• 文件: py draw.py result.json
    生成同名 HTML 文件
• 目录: py draw.py directory/
    遍历目录中所有 JSON 文件生成对应的 HTML 文件
"""

import sys
import json
import pathlib

import numpy as np
import plotly.graph_objects as go
import plotly.subplots
import plotly.express.colors

COLOR_MAP = {}  # 全局颜色映射表，确保同一类别在不同图中颜色一致


def draw_file(file_path: str):
    """绘制3D装箱图"""

    with open(file_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    containers = data["result"]["containers"]
    # 构建箱型尺寸查找表
    box_types = {bt["id"]: bt["size"] for bt in data["result"].get("box_types", [])}
    COLOR_MAP.clear()  # 清空全局颜色映射表，确保不同文件间颜色独立

    # 计算绘图相关参数
    max_dim = max(c["inner_size"][k] for c in containers for k in ("x", "y", "z"))
    n = len(containers)
    cols = min(4, n)  # 最多4列
    rows = (n + cols - 1) // cols

    # 创建子图
    subplot_titles = []
    for c in containers:
        title = f"Container {c['type_id']}<br>"
        title += f"<sub>Volume Rate: {c['volume_rate']:.2%}"
        if c.get("weight_rate") is not None:
            title += f", Weight Rate: {c['weight_rate']:.2%}"
        title += f"<br>Packed: {c['packed_count']}"
        platforms = c.get("platforms", [])
        groups = c.get("groups", [])
        if platforms:
            title += f"<br>Route: {', '.join(platforms)}"
        if groups:
            title += f"<br>Groups: {', '.join(groups)}"
        title += "</sub>"
        subplot_titles.append(title)

    fig = plotly.subplots.make_subplots(
        rows=rows, cols=cols,
        specs=[[{"type": "scatter3d"} for _ in range(cols)] for _ in range(rows)],
        subplot_titles=subplot_titles,
    )

    # 绘制每个装箱方案
    mesh_trace_info = []  # 收集 Mesh3d 轨迹信息用于颜色切换
    container_trace_ranges = []  # 每个容器的 trace 索引范围 [start, end)
    shown_legends = set()
    legend_keys = {"type", "platform", "group"}  # 收集各维度实际出现的值用于自动切换
    seen_values = {k: set() for k in legend_keys}
    for i, container in enumerate(containers):
        trace_start = len(fig.data)  # type: ignore
        row = i // cols + 1
        col = i % cols + 1
        draw(container, fig, row, col, max_dim, shown_legends, mesh_trace_info, box_types, seen_values)
        trace_end = len(fig.data)  # type: ignore
        container_trace_ranges.append((trace_start, trace_end))

    # 添加容器选择、视图切换、颜色模式切换（按上下顺序排列）
    add_container_selector(fig, containers, container_trace_ranges)
    add_view_selector(fig, rows, cols)
    add_color_selector(fig, mesh_trace_info)

    # 自动切换：如果某维度只有一种值，默认用下一个多样化的维度
    auto_key = None
    for k in ["platform", "group", "type"]:
        if len(seen_values.get(k, set())) > 1:
            auto_key = k
            break
    if auto_key is not None and auto_key != "type":
        auto_args = _build_coloring_args(mesh_trace_info, key=auto_key)
        mesh_indices = [info["idx"] for info in mesh_trace_info]
        for idx, trace_idx in enumerate(mesh_indices):
            fig.data[trace_idx].update(
                color=auto_args["color"][idx],
                name=auto_args["name"][idx],
                legendgroup=auto_args["legendgroup"][idx],
                showlegend=auto_args["showlegend"][idx],
            )
        # 激活对应的菜单项: 0=type, 1=platform, 2=group
        menu_index = ["type", "platform", "group"].index(auto_key)
        fig.layout.updatemenus[-1].active = menu_index  # type: ignore

    # 保存文件
    fig.write_html(file_path.replace(".json", ".html"))
    print(f"Saved: {file_path.replace('.json', '.html')}")


def draw(container: dict, fig: go.Figure, row: int, col: int, max_dim: int,
         shown_legends: set, mesh_trace_info: list[dict], box_types: dict[str, dict],
         seen_values: dict[str, set]):
    """绘制容器和箱子"""

    # 绘制容器
    draw_container(fig, container, row, col)

    # 绘制箱子
    for placement in container["placements"]:
        draw_box(fig, placement, row, col, shown_legends, mesh_trace_info, box_types, seen_values)

    # 设置场景
    inner = container["inner_size"]
    scene_config = dict(
        xaxis=dict(range=[0, inner["x"]], title="X"),
        yaxis=dict(range=[0, inner["y"]], title="Y"),
        zaxis=dict(range=[0, inner["z"]], title="Z"),
        aspectratio=dict(
            x=inner["x"] / max_dim,
            y=inner["y"] / max_dim,
            z=inner["z"] / max_dim
        )
    )
    scene = f"scene{(row-1)*4 + col}" if (row-1)*4 + col > 1 else "scene"
    fig.layout[scene].update(scene_config)


def draw_container(fig: go.Figure, container: dict, row: int, col: int):
    """绘制容器"""
    inner = container["inner_size"]
    l, w, h = inner["x"], inner["y"], inner["z"]

    vertices = np.array([
        [0, 0, 0], [l, 0, 0], [l, w, 0], [0, w, 0],  # 底面4个顶点
        [0, 0, h], [l, 0, h], [l, w, h], [0, w, h]   # 顶面4个顶点
    ])
    line = dict(color='gray', width=2)
    draw_edges(fig, vertices, line, row, col)


def draw_box(fig: go.Figure, placement: dict, row: int, col: int,
             shown_legends: set, mesh_trace_info: list[dict], box_types: dict[str, dict],
             seen_values: dict[str, set]):
    """绘制箱子"""

    pos = placement["position"]
    x, y, z = pos["x"], pos["y"], pos["z"]
    orient = placement["orientation"]
    size = box_types.get(placement["box_type_id"], {"x": 0, "y": 0, "z": 0})
    l, w, h = get_oriented_dim(size["x"], size["y"], size["z"], orient)
    box_type_id = placement["box_type_id"]
    color = get_color(box_type_id)

    # 定义长方体的8个顶点
    vertices = np.array([
        [x, y, z], [x + l, y, z], [x + l, y + w, z], [x, y + w, z],
        [x, y, z + h], [x + l, y, z + h], [x + l, y + w, z + h], [x, y + w, z + h],
    ])

    # 定义6个面的顶点索引（每个面由2个三角形组成）
    faces = [
        [0, 1, 2], [0, 2, 3],  # 底面
        [4, 5, 6], [4, 6, 7],  # 顶面
        [0, 1, 5], [0, 5, 4],  # 前面
        [3, 2, 6], [3, 6, 7],  # 后面
        [0, 3, 7], [0, 7, 4],  # 左面
        [1, 2, 6], [1, 6, 5],  # 右面
    ]

    # 收集各维度值用于自动切换判断
    platform_val = placement.get("platform") or "(none)"
    group_val = placement.get("group") or "(none)"
    seen_values["type"].add(box_type_id)
    seen_values["platform"].add(platform_val)
    seen_values["group"].add(group_val)

    fig.add_trace(
        go.Mesh3d(
            x=vertices[:, 0],
            y=vertices[:, 1],
            z=vertices[:, 2],
            i=[face[0] for face in faces],
            j=[face[1] for face in faces],
            k=[face[2] for face in faces],
            color=color,
            name=box_type_id,
            legendgroup=box_type_id,
            showlegend=box_type_id not in shown_legends,
            text=get_text(placement, size, l, w, h),
            hoverinfo='text',
        ),
        row=row, col=col
    )
    mesh_trace_info.append({
        "idx": len(fig.data) - 1,  # type: ignore
        "type": box_type_id,
        "platform": platform_val,
        "group": group_val,
    })
    shown_legends.add(box_type_id)

    # 绘制边框
    line = dict(color='black', width=1)
    draw_edges(fig, vertices, line, row, col)


def draw_edges(fig: go.Figure, vertices: np.ndarray, line: dict, row: int, col: int):
    """绘制长方体边框"""
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),  # 底面
        (4, 5), (5, 6), (6, 7), (7, 4),  # 顶面
        (0, 4), (1, 5), (2, 6), (3, 7),  # 竖直边
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


def get_text(placement: dict, size: dict, l: int, w: int, h: int) -> str:
    """生成用于鼠标悬浮显示的文本信息"""
    pos = placement["position"]
    text = f"box: {placement['box_id']}<br>"
    text += f"type: {placement['box_type_id']}<br>"
    text += f"pos: ({pos['x']}, {pos['y']}, {pos['z']})<br>"
    text += f"size: {size['x']}x{size['y']}x{size['z']}<br>"
    text += f"orient: {placement['orientation']}<br>"
    text += f"placed: {l}x{w}x{h}<br>"
    platform_val = placement.get("platform")
    group_val = placement.get("group")
    if platform_val:
        text += f"platform: {platform_val}<br>"
    if group_val:
        text += f"group: {group_val}"
    return text


def get_color(category: str):
    """一个类别一种颜色"""
    if category not in COLOR_MAP:
        base = plotly.express.colors.qualitative.Plotly
        COLOR_MAP[category] = base[len(COLOR_MAP) % len(base)]
    return COLOR_MAP[category]


def get_oriented_dim(x: int, y: int, z: int, orient: str) -> tuple[int, int, int]:
    """根据 orientation 字符串获取实际摆放的长宽高"""
    orient_map = {
        "xyz": (x, y, z),
        "yxz": (y, x, z),
        "xzy": (x, z, y),
        "zxy": (z, x, y),
        "yzx": (y, z, x),
        "zyx": (z, y, x),
    }
    return orient_map[orient]


def add_view_selector(fig: go.Figure, rows: int, cols: int):
    """添加视图选择器"""

    # 定义标准视图的相机参数
    views = {
        "默认视图": None,
        "前视图": {"eye": {"x": 0, "y": -2, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "后视图": {"eye": {"x": 0, "y": 2, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "左视图": {"eye": {"x": -2, "y": 0, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "右视图": {"eye": {"x": 2, "y": 0, "z": 0}, "up": {"x": 0, "y": 0, "z": 1}},
        "俯视图": {"eye": {"x": 0, "y": 0, "z": 2}, "up": {"x": 0, "y": 1, "z": 0}},
        "仰视图": {"eye": {"x": 0, "y": 0, "z": -2}, "up": {"x": 0, "y": -1, "z": 0}},
    }

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

    menus = list(fig.layout.updatemenus or [])  # type: ignore
    menus.append(dict(
        buttons=buttons,
        direction="down",
        showactive=True,
        active=0,
        xanchor="left",
        yanchor="top",
        x=0,
        y=0.95,
    ))
    fig.update_layout(updatemenus=menus)


def add_color_selector(fig: go.Figure, mesh_trace_info: list[dict]):
    """添加颜色模式选择器（箱型 / 平台 / 分组）

    通过 updatemenus + restyle 在不重新绘制的情况下切换颜色。
    """

    if not mesh_trace_info:
        return

    mesh_indices = [info["idx"] for info in mesh_trace_info]

    # 构建三种模式下的颜色与图例参数
    modes = [("type", "箱型上色"), ("platform", "平台上色"), ("group", "分组上色")]
    buttons = [
        dict(label=label, method="restyle", args=[_build_coloring_args(mesh_trace_info, key=k), mesh_indices])
        for k, label in modes
    ]

    color_menu = dict(
        buttons=buttons,
        direction="down",
        showactive=True,
        active=0,
        xanchor="left",
        yanchor="top",
        x=0,
        y=0.90,
    )

    menus = list(fig.layout.updatemenus or [])  # type: ignore
    menus.append(color_menu)
    fig.update_layout(updatemenus=menus)


def _build_showlegend_for_traces(fig: go.Figure, trace_indices: list[int]) -> list[bool]:
    """为指定 trace 列表生成 showlegend 数组：只对 Mesh3d 显示图例，重复 group 只显示一次。"""
    showlegend = [False] * len(fig.data)  # type: ignore
    seen_groups = set()
    for i in trace_indices:
        t = fig.data[i]
        if isinstance(t, go.Mesh3d):
            group = t.legendgroup or ""
            if group not in seen_groups:
                showlegend[i] = True
                seen_groups.add(group)
    return showlegend


def add_container_selector(fig: go.Figure, containers: list[dict], trace_ranges: list[tuple[int, int]]):
    """添加容器选择下拉菜单（全部 / 指定容器）

    选择单容器时将该场景 domain 扩至全屏，实现放大效果。
    返回"全部容器"时恢复原始场景布局。
    """

    total_traces = len(fig.data)  # type: ignore
    n = len(containers)
    if total_traces == 0 or n == 0:
        return

    # 收集原始 scene domain，用于恢复
    scene_keys = ["scene"] + [f"scene{i+1}" for i in range(1, n)]
    original_domains = {}
    for sk in scene_keys:
        if sk in fig.layout:
            d = fig.layout[sk].domain
            original_domains[f"{sk}.domain.x"] = [d.x[0], d.x[1]]
            original_domains[f"{sk}.domain.y"] = [d.y[0], d.y[1]]

    # 收集原始 annotation 配置，用于恢复
    annotations = list(fig.layout.annotations) if fig.layout.annotations else []
    orig_ann = {}
    for i, a in enumerate(annotations):
        orig_ann[f"annotations[{i}].text"] = a.text
        orig_ann[f"annotations[{i}].x"] = a.x
        orig_ann[f"annotations[{i}].y"] = a.y

    buttons = []
    all_indices = list(range(total_traces))
    all_showlegend = _build_showlegend_for_traces(fig, all_indices)

    # "全部容器"：恢复原始场景布局、标题
    restore_layout = dict(original_domains)
    restore_layout.update(orig_ann)
    buttons.append(dict(
        label="全部容器",
        method="update",
        args=[
            {"visible": True, "showlegend": all_showlegend},
            restore_layout,
            all_indices,
        ],
    ))

    for ci, (start, end) in enumerate(trace_ranges):
        scene_key = scene_keys[ci]
        container_indices = list(range(start, end))
        vis = [False] * total_traces
        for i in container_indices:
            vis[i] = True
        showlegend = _build_showlegend_for_traces(fig, container_indices)

        # 将该场景 domain 扩至全屏，标题居中，其余标题隐藏
        single_layout = {
            f"{scene_key}.domain.x": [0, 1],
            f"{scene_key}.domain.y": [0, 1],
        }
        for i, a in enumerate(annotations):
            if i == ci:
                single_layout[f"annotations[{i}].x"] = 0.5  # type: ignore
                single_layout[f"annotations[{i}].text"] = a.text
            else:
                single_layout[f"annotations[{i}].text"] = ""  # type: ignore

        buttons.append(dict(
            label=f"#{ci+1}",
            method="update",
            args=[
                {"visible": vis, "showlegend": showlegend},
                single_layout,
                list(range(total_traces)),
            ],
        ))

    container_menu = dict(
        buttons=buttons,
        direction="down",
        showactive=True,
        active=0,
        xanchor="left",
        yanchor="top",
        x=0,
        y=1.0,
    )

    fig.update_layout(updatemenus=[container_menu])


def _build_coloring_args(mesh_trace_info: list[dict], key: str) -> dict:
    """根据指定键（type / platform / group）为每个 Mesh3d 构建颜色与图例参数数组。

    返回一个用于 plotly updatemenus/restyle 的参数 dict：
    {"color": [...], "name": [...], "legendgroup": [...], "showlegend": [...]}。
    """
    values = [info[key] for info in mesh_trace_info]

    colors = []
    names = []
    groups = []
    showlegends = []
    seen = set()

    for val in values:
        colors.append(get_color(val))
        names.append(str(val))
        groups.append(str(val))
        showlegends.append(val not in seen)
        seen.add(val)

    return {
        "color": colors,
        "name": names,
        "legendgroup": groups,
        "showlegend": showlegends,
    }


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: py draw.py <file|directory>")
        sys.exit(0)

    path = pathlib.Path(sys.argv[1])
    if path.is_file():
        draw_file(str(path))
    elif path.is_dir():
        files = sorted(p for p in path.iterdir() if p.suffix.lower() == ".json")
        if not files:
            print(f"No json files found in directory: {path}")
            sys.exit(0)
        for file in files:
            draw_file(str(file))
    else:
        print(f"Path not found: {path}")
        sys.exit(0)
