"""pack3d-api: 三维装箱求解器 HTTP API 服务。"""

import json
from contextlib import asynccontextmanager
from pathlib import Path
from urllib.parse import quote

from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel, Field

from .manager import InstanceManager


@asynccontextmanager
async def lifespan(_: FastAPI):
    yield
    manager.shutdown()


app = FastAPI(title="pack3d-api", lifespan=lifespan)
manager = InstanceManager()

# --- info endpoints (no auth, always available) ---


@app.get("/api/info")
def api_info():
    return {
        "name": "pack3d-api",
        "version": "0.1.0",
        "algorithms": ["gep", "glc", "rgs", "bsg"],
        "objectives": ["min_container_count", "min_platform_split", "max_volume_rate", "min_group_split"],
        "orientations": ["xyz", "xzy", "yxz", "yzx", "zxy", "zyx"],
    }


@app.get("/api/schema")
def api_schema():
    schema_path = Path("data/input_schema.json")
    if not schema_path.exists():
        raise HTTPException(404, "input_schema.json not found")
    return json.loads(schema_path.read_text(encoding="utf-8"))


# --- request models ---

class NameRequest(BaseModel):
    name: str = Field(min_length=1, max_length=128)


class CreateRunRequest(BaseModel):
    input_json: dict = Field(description="pack3d 输入 JSON 对象")
    random_seed: int = 42
    run_name: str | None = None


# helpers

def _get_instance_or_404(instance_id: str):
    instance = manager.get_instance(instance_id)
    if instance is None:
        raise HTTPException(404, "instance not found")
    return instance


def _get_run_or_404(instance_id: str, run_id: str):
    state = manager.get_run(run_id)
    if state is None or state.instance_id != instance_id:
        raise HTTPException(404, "run not found")
    return state


# instances

@app.post("/api/instances", status_code=201)
def create_instance(req: NameRequest):
    state = manager.create_instance(req.name)
    return state.to_dict()


@app.get("/api/instances")
def list_instances():
    return {"instances": [s.to_dict() for s in manager.list_instances()]}


@app.get("/api/instances/{instance_id}")
def get_instance(instance_id: str):
    state = _get_instance_or_404(instance_id)
    return state.to_dict(include_runs=True)


@app.patch("/api/instances/{instance_id}")
def rename_instance(instance_id: str, req: NameRequest):
    state = _get_instance_or_404(instance_id)
    state = manager.rename_instance(state, req.name)
    return state.to_dict()


@app.delete("/api/instances/{instance_id}")
def delete_instance(instance_id: str):
    state = _get_instance_or_404(instance_id)
    manager.delete_instance(state)
    return Response(status_code=204)


# runs

@app.post("/api/instances/{instance_id}/runs", status_code=201)
def create_run(instance_id: str, req: CreateRunRequest):
    instance = _get_instance_or_404(instance_id)
    input_str = json.dumps(req.input_json, ensure_ascii=False)
    state = manager.create_run(instance, input_str, req.random_seed, req.run_name)
    return state.to_dict(instance.instance_name)


@app.get("/api/instances/{instance_id}/runs")
def list_runs(instance_id: str):
    instance = _get_instance_or_404(instance_id)
    return {"runs": [r.to_dict(instance.instance_name) for r in manager.list_runs(instance)]}


@app.get("/api/instances/{instance_id}/runs/{run_id}")
def get_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    instance = _get_instance_or_404(instance_id)
    return state.to_dict(instance.instance_name)


@app.get("/api/instances/{instance_id}/runs/{run_id}/result")
def get_result(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    if state.status not in ("completed", "invalid"):
        raise HTTPException(409, f"run is {state.status}, result not ready")
    result = state.get_result()
    if result is None:
        raise HTTPException(404, "result not found")
    return result


@app.get("/api/instances/{instance_id}/runs/{run_id}/result/download")
def download_result(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    if state.status not in ("completed", "invalid"):
        raise HTTPException(409, f"run is {state.status}, result not ready")
    result = state.get_result()
    if result is None:
        raise HTTPException(404, "result not found")

    filename = f"{state.run_name}-result.json"
    encoded = quote(filename)
    content = json.dumps(result, indent=2, ensure_ascii=False)

    return Response(
        content,
        media_type="application/json",
        headers={"Content-Disposition": f"attachment; filename*=UTF-8''{encoded}"},
    )


@app.get("/api/instances/{instance_id}/runs/{run_id}/input")
def get_input(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    input_file = state.run_dir / "input.json"
    if not input_file.exists():
        raise HTTPException(404, "input not found")
    return Response(
        input_file.read_text(encoding="utf-8"),
        media_type="application/json",
    )


@app.patch("/api/instances/{instance_id}/runs/{run_id}")
def rename_run(instance_id: str, run_id: str, req: NameRequest):
    state = _get_run_or_404(instance_id, run_id)
    state = manager.rename_run(state, req.name)
    instance = _get_instance_or_404(instance_id)
    return state.to_dict(instance.instance_name)


@app.post("/api/instances/{instance_id}/runs/{run_id}/cancel")
def cancel_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    if state.status != "running":
        raise HTTPException(409, f"run is {state.status}, cannot cancel")
    state = manager.stop_run(state)
    instance = _get_instance_or_404(instance_id)
    return state.to_dict(instance.instance_name)


@app.delete("/api/instances/{instance_id}/runs/{run_id}")
def delete_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    manager.delete_run(state)
    return Response(status_code=204)
