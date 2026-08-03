import argparse
import json
import sys
from pathlib import Path

import pack3d


# 与 C++ CLI 一致：JSON 已显式指定该节点时，CLI 参数报错退出
# path 为 JSON 路径（如 "constraints", "time_limit"）；value 为 None 表示未传，跳过
def try_set(input_data, cli_arg, value, *path):
    if value is None:
        return True
    node = input_data
    for key in path[:-1]:
        node = node.setdefault(key, {})
    if path[-1] in node:
        print(f"Error: CLI '{cli_arg}' not allowed when JSON '{'/'.join(path)}' is specified.")
        return False
    node[path[-1]] = value
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser("pack3d")
    parser.add_argument(
        "file",
        help="Path to the JSON file.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="output",
        help="Directory to write results into.",
    )
    parser.add_argument(
        "-a",
        "--algorithm",
        choices=["gep", "glc", "rgs", "bsg"],
        help="Algorithm to use.",
    )
    parser.add_argument(
        "-t",
        "--time-limit",
        type=float,
        help="Time limit in seconds.",
    )
    parser.add_argument(
        "-s",
        "--support-rate",
        type=float,
        help="Support rate (0~1).",
    )
    parser.add_argument(
        "--platform-limit",
        type=int,
        help="Platform limit.",
    )
    parser.add_argument(
        "--tender-limit",
        type=int,
        help="Tender limit.",
    )
    args = parser.parse_args()

    with open(args.file, encoding="utf-8") as f:
        input_data = json.load(f)

    ok = True
    ok &= try_set(input_data, "-a", args.algorithm, "algorithm")
    ok &= try_set(input_data, "-t", args.time_limit, "constraints", "time_limit")
    ok &= try_set(input_data, "-s", args.support_rate, "constraints", "support_rate")
    ok &= try_set(input_data, "--platform-limit", args.platform_limit, "constraints", "platform_limit")
    ok &= try_set(input_data, "--tender-limit", args.tender_limit, "constraints", "tender_limit")
    if not ok:
        sys.exit(1)

    result = pack3d.run(input_data)

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{Path(args.file).stem}_result.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)
    print(f"Result written to: {out_file}")
