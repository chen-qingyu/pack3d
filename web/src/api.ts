export type RunStatus = 'running' | 'completed' | 'invalid' | 'failed' | 'cancelled'

export interface Summary {
  elapsed_second?: number
  packed_box_count: number
  unpacked_box_count: number
  container_count: number
  platform_split: number
  volume_rate: number
  group_split: number
}

export interface Run {
  run_id: string
  instance_id: string
  instance_name: string
  run_name: string
  status: RunStatus
  random_seed: number
  error: string | null
  created_at: string
  summary?: Summary
  violations?: string[]
}

export interface Instance {
  instance_id: string
  instance_name: string
  created_at: string
  run_count: number
  runs?: Run[]
}

export interface Placement {
  box_id: string
  box_type_id: string
  x: number
  y: number
  z: number
  dx: number
  dy: number
  dz: number
  orientation: string
  platform?: string | null
  group?: string | null
}

export interface ContainerResult {
  type_id: string
  sx: number
  sy: number
  sz: number
  max_weight?: number | null
  used_volume?: number
  used_weight?: number | null
  volume_rate: number
  weight_rate?: number | null
  packed_count: number
  platforms?: string[]
  groups?: string[]
  placements: Placement[]
}

export interface ResultData {
  status: string
  summary: Summary
  result?: {
    containers: ContainerResult[]
    box_types?: Array<{ id: string; sx: number; sy: number; sz: number }>
    unpacked_boxes?: string[]
  }
  violations?: string[]
}

const request = async <T>(path: string, init?: RequestInit): Promise<T> => {
  const response = await fetch(path, {
    ...init,
    headers: { 'Content-Type': 'application/json', ...init?.headers },
  })
  if (!response.ok) {
    let message = `${response.status} ${response.statusText}`
    try {
      const body = await response.json() as { detail?: string }
      if (body.detail) message = body.detail
    } catch {
      // Keep the HTTP status when the server does not return JSON.
    }
    throw new Error(message)
  }
  if (response.status === 204) return undefined as T
  return response.json() as Promise<T>
}

export const api = {
  listInstances: () => request<{ instances: Instance[] }>('/api/instances'),
  getInstance: (id: string) => request<Instance>(`/api/instances/${id}`),
  createInstance: (name: string) => request<Instance>('/api/instances', {
    method: 'POST', body: JSON.stringify({ name }),
  }),
  renameInstance: (id: string, name: string) => request<Instance>(`/api/instances/${id}`, {
    method: 'PATCH', body: JSON.stringify({ name }),
  }),
  deleteInstance: (id: string) => request<void>(`/api/instances/${id}`, { method: 'DELETE' }),
  createRun: (id: string, inputJson: Record<string, unknown>, randomSeed: number, runName: string) =>
    request<Run>(`/api/instances/${id}/runs`, {
      method: 'POST', body: JSON.stringify({ input_json: inputJson, random_seed: randomSeed, run_name: runName }),
    }),
  getRun: (instanceId: string, runId: string) => request<Run>(`/api/instances/${instanceId}/runs/${runId}`),
  getResult: (instanceId: string, runId: string) => request<ResultData>(`/api/instances/${instanceId}/runs/${runId}/result`),
  getInput: (instanceId: string, runId: string) => request<Record<string, unknown>>(`/api/instances/${instanceId}/runs/${runId}/input`),
  renameRun: (instanceId: string, runId: string, name: string) => request<Run>(`/api/instances/${instanceId}/runs/${runId}`, {
    method: 'PATCH', body: JSON.stringify({ name }),
  }),
  cancelRun: (instanceId: string, runId: string) => request<Run>(`/api/instances/${instanceId}/runs/${runId}/cancel`, { method: 'POST' }),
  deleteRun: (instanceId: string, runId: string) => request<void>(`/api/instances/${instanceId}/runs/${runId}`, { method: 'DELETE' }),
  downloadResult: (instanceId: string, runId: string) => `/api/instances/${instanceId}/runs/${runId}/result/download`,
}
