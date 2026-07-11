"""实例与运行生命周期管理。"""

import json
import multiprocessing as mp
import re
import shutil
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import db

INSTANCES_ROOT = Path("instances")


@dataclass
class RunState:
    run_id: str
    instance_id: str
    run_name: str
    status: str          # running | completed | failed | cancelled
    random_seed: int
    process: mp.Process | None
    run_dir: Path
    created_at: str
    error: str | None = None
    pipe: Any = None
    result: dict | None = None

    def to_dict(self, instance_name: str = "") -> dict:
        d = {
            "run_id": self.run_id,
            "instance_id": self.instance_id,
            "instance_name": instance_name,
            "run_name": self.run_name,
            "status": self.status,
            "random_seed": self.random_seed,
            "error": self.error,
            "created_at": self.created_at,
        }
        if self.status in ("completed", "invalid") and self.result is not None:
            d["summary"] = self.result["summary"]
        if self.status == "invalid" and self.result is not None:
            d["violations"] = self.result.get("violations", [])
        return d

    def get_result(self) -> dict | None:
        if self.status not in ("completed", "invalid"):
            return None
        result_file = self.run_dir / "output" / "result.json"
        if result_file.exists():
            return json.loads(result_file.read_text(encoding="utf-8"))
        return None


@dataclass
class InstanceState:
    instance_id: str
    instance_name: str
    created_at: str
    runs: dict[str, RunState] = field(default_factory=dict)

    def to_dict(self, include_runs: bool = False) -> dict:
        d = {
            "instance_id": self.instance_id,
            "instance_name": self.instance_name,
            "created_at": self.created_at,
            "run_count": len(self.runs),
        }
        if include_runs:
            d["runs"] = [r.to_dict(self.instance_name) for r in sorted(
                self.runs.values(), key=lambda r: r.created_at, reverse=True)]
        return d


def _run_worker(input_json: str, output_dir: str, random_seed: int, conn):
    """子进程入口：加载 pack3d 并运行求解器。"""
    import random
    random.seed(random_seed)

    import pack3d
    input_data = json.loads(input_json)
    result = pack3d.run(input_data)

    out_path = Path(output_dir)
    out_path.mkdir(parents=True, exist_ok=True)
    (out_path / "result.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")

    # core 的 "complete" / "partial" / "timeout" / "blocked" 都是正常完成
    # 只有 "invalid" 是输入问题
    core_status = result["status"]
    run_status = "invalid" if core_status == "invalid" else "completed"
    conn.send({"status": run_status, "result": result})
    conn.close()


class InstanceManager:
    def __init__(self):
        self._instances: dict[str, InstanceState] = {}
        self._runs: dict[str, RunState] = {}
        self._conn = db.init_db()
        self._load_all()

    def shutdown(self):
        for run in list(self._runs.values()):
            self._kill_run(run)
        self._conn.close()

    def _load_all(self):
        for row in db.list_instances(self._conn):
            state = InstanceState(
                instance_id=row["instance_id"],
                instance_name=row["instance_name"],
                created_at=row["created_at"],
            )
            self._instances[state.instance_id] = state
        for row in db.list_runs(self._conn):
            instance = self._instances.get(row["instance_id"])
            if instance is None:
                continue
            run = RunState(
                run_id=row["run_id"],
                instance_id=row["instance_id"],
                run_name=row["run_name"],
                status=row["status"],
                random_seed=row["random_seed"],
                process=None,
                run_dir=INSTANCES_ROOT / row["instance_id"] / "runs" / row["run_id"],
                created_at=row["created_at"],
                error=row.get("error"),
            )
            # 进程已不在，将 running 标记为 failed
            if run.status == "running":
                run.status = "failed"
                db.update_run(self._conn, run.run_id, status="failed")
            instance.runs[run.run_id] = run
            self._runs[run.run_id] = run

    # instances

    def create_instance(self, instance_name: str) -> InstanceState:
        instance_name = re.sub(r'[\\/:*?"<>|]', '_', instance_name)
        instance_id = uuid.uuid4().hex[:12]
        created_at = datetime.now(timezone.utc).isoformat()
        (INSTANCES_ROOT / instance_id).mkdir(parents=True)
        db.insert_instance(self._conn, instance_id, instance_name, created_at)

        state = InstanceState(instance_id=instance_id, instance_name=instance_name, created_at=created_at)
        self._instances[instance_id] = state
        return state

    def get_instance(self, instance_id: str) -> InstanceState | None:
        return self._instances.get(instance_id)

    def list_instances(self) -> list[InstanceState]:
        return sorted(self._instances.values(), key=lambda s: s.created_at, reverse=True)

    def rename_instance(self, state: InstanceState, instance_name: str) -> InstanceState:
        instance_name = re.sub(r'[\\/:*?"<>|]', '_', instance_name)
        state.instance_name = instance_name
        db.update_instance(self._conn, state.instance_id, instance_name)
        return state

    def delete_instance(self, state: InstanceState) -> InstanceState:
        for run in list(state.runs.values()):
            self._kill_run(run)
        shutil.rmtree(INSTANCES_ROOT / state.instance_id, ignore_errors=True)
        db.delete_instance(self._conn, state.instance_id)
        for run in state.runs.values():
            self._runs.pop(run.run_id, None)
        del self._instances[state.instance_id]
        return state

    # runs

    def create_run(self, instance: InstanceState, input_json: str,
                   random_seed: int = 42,
                   run_name: str | None = None) -> RunState:
        run_id = uuid.uuid4().hex[:12]
        if run_name is None:
            run_name = instance.instance_name
        run_dir = INSTANCES_ROOT / instance.instance_id / "runs" / run_id
        out_dir = run_dir / "output"
        out_dir.mkdir(parents=True)
        created_at = datetime.now(timezone.utc).isoformat()

        (run_dir / "input.json").write_text(input_json, encoding="utf-8")

        db.insert_run(self._conn, run_id, instance.instance_id, run_name, "running", random_seed, created_at)

        parent_conn, child_conn = mp.Pipe()
        p = mp.Process(
            target=_run_worker,
            args=(input_json, str(out_dir), random_seed, child_conn)
        )
        p.start()

        state = RunState(
            run_id=run_id, instance_id=instance.instance_id, run_name=run_name,
            status="running", random_seed=random_seed, process=p,
            pipe=parent_conn, run_dir=run_dir, created_at=created_at,
        )
        instance.runs[run_id] = state
        self._runs[run_id] = state
        return state

    def get_run(self, run_id: str) -> RunState | None:
        state = self._runs.get(run_id)
        if state is None:
            return None
        self._refresh_run(state)
        return state

    def list_runs(self, instance: InstanceState | None = None) -> list[RunState]:
        runs = self._runs.values()
        if instance:
            runs = [r for r in runs if r.instance_id == instance.instance_id]
        for r in runs:
            self._refresh_run(r)
        return sorted(runs, key=lambda r: r.created_at, reverse=True)

    def rename_run(self, state: RunState, run_name: str) -> RunState:
        run_name = re.sub(r'[\\/:*?"<>|]', '_', run_name)
        state.run_name = run_name
        db.update_run(self._conn, state.run_id, run_name=run_name)
        return state

    def stop_run(self, state: RunState) -> RunState:
        self._kill_run(state)
        state.status = "cancelled"
        db.update_run(self._conn, state.run_id, status="cancelled")
        shutil.rmtree(state.run_dir / "output", ignore_errors=True)
        return state

    def delete_run(self, state: RunState) -> RunState:
        self._kill_run(state)
        shutil.rmtree(state.run_dir, ignore_errors=True)
        db.delete_run(self._conn, state.run_id)
        instance = self._instances.get(state.instance_id)
        if instance:
            instance.runs.pop(state.run_id, None)
        self._runs.pop(state.run_id, None)
        return state

    # internal

    def _kill_run(self, state: RunState):
        if state.pipe:
            state.pipe.close()
            state.pipe = None
        if state.process and state.process.is_alive():
            state.process.terminate()
            state.process.join(timeout=5)

    def _refresh_run(self, state: RunState):
        if state.status != "running":
            return
        if state.pipe and state.pipe.poll():
            msg = state.pipe.recv()
            state.status = msg["status"]
            state.result = msg["result"]
            db.update_run(self._conn, state.run_id, status=state.status)
            self._kill_run(state)
