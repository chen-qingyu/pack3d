import json
import os


def parse_br_file(file):
    """将 BR 格式文件转换为一组标准 hypercube JSON 输入"""
    results = []

    with open(file, 'r') as f:
        lines = [line.strip() for line in f if line.strip()]

    current_line = 1
    for _ in range(int(lines[0])):
        # 案例元数据
        problem_num = int(lines[current_line].split()[0])
        current_line += 1

        # 容器内部尺寸
        cx, cy, cz = map(int, lines[current_line].split())
        current_line += 1

        # 箱子类型数量
        num_box_types = int(lines[current_line])
        current_line += 1

        # 容器类型
        container_id = f"{os.path.basename(file).split('.')[0]}#{problem_num}"
        container_type = {
            "id": container_id,
            "inner_size": {"x": cx, "y": cy, "z": cz},
            "max_weight": 10000000.0,
            "quantity_limit": 1,
        }

        # 箱子类型
        box_types = []
        box_type_map = {}
        box_counts = []

        for i in range(num_box_types):
            parts = list(map(int, lines[current_line].split()))
            current_line += 1
            bt_id = f"t{i + 1}"

            bx, by, bz = parts[1], parts[3], parts[5]
            flag_x, flag_y, flag_z = parts[2], parts[4], parts[6]

            orients = []
            if flag_z == 1:
                orients += ["xyz", "yxz"]
            if flag_y == 1:
                orients += ["xzy", "zxy"]
            if flag_x == 1:
                orients += ["yzx", "zyx"]

            box_types.append({
                "id": bt_id,
                "size": {"x": bx, "y": by, "z": bz},
                "allowed_orientations": orients,
            })
            box_type_map[i] = bt_id
            box_counts.append(parts[7])

        # 箱子实例
        boxes = []
        box_id = 1
        for i in range(num_box_types):
            for _ in range(box_counts[i]):
                boxes.append({
                    "id": f"b{box_id}",
                    "box_type_id": box_type_map[i],
                    "weight": 1.0,
                })
                box_id += 1

        result = {
            "container_types": [container_type],
            "box_types": box_types,
            "boxes": boxes,
        }
        results.append((problem_num, result))

    return results


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    br_origin = os.path.join(root, "data", "br-origin")
    br_out = os.path.join(root, "data", "br")

    if os.path.exists(br_out):
        print(f"{br_out} already exists.")
        return

    os.makedirs(br_out)

    for i in range(16):
        src = os.path.join(br_origin, f"br{i}.txt")
        if not os.path.isfile(src):
            print(f"SKIP: {src} not found")
            continue

        results = parse_br_file(src)

        for problem_num, data in results:
            dst = os.path.join(br_out, f"br{i:02d}_{problem_num:03d}.json")
            with open(dst, "w") as f:
                json.dump(data, f, indent=2)

        print(f"OK: br{i}.txt -> {len(results)} json files")


if __name__ == "__main__":
    main()
