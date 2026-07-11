import argparse
import json
from pathlib import Path

import pack3d


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
        default=120.0,
        type=float,
        help="Time limit in seconds.",
    )
    parser.add_argument(
        "-s",
        "--support-rate",
        default=0.0,
        type=float,
        help="Support rate (0~1).",
    )
    args = parser.parse_args()

    with open(args.file, encoding="utf-8") as f:
        input_data = json.load(f)

    if args.algorithm:
        input_data["algorithm"] = args.algorithm
    if args.time_limit is not None:
        input_data.setdefault("constraints", {})["time_limit"] = args.time_limit
    if args.support_rate != 0.0:
        input_data.setdefault("constraints", {})["support_rate"] = args.support_rate

    result = pack3d.run(input_data)

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{Path(args.file).stem}_result.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)
    print(f"Result written to: {out_file}")
