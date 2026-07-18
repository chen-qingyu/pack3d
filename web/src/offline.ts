import type { ResultData } from './api'

type JsonObject = Record<string, unknown>

const isObject = (value: unknown): value is JsonObject => typeof value === 'object' && value !== null

export function parseOfflineResult(text: string): ResultData {
    let value: unknown
    try {
        value = JSON.parse(text)
    } catch (reason) {
        throw new Error(reason instanceof Error ? `JSON 语法错误：${reason.message}` : 'JSON 语法错误')
    }

    if (!isObject(value) || !isObject(value.result) || !Array.isArray(value.result.containers)) {
        throw new Error('请选择 pack3d 输出结果 JSON，其中必须包含 result.containers')
    }

    for (const container of value.result.containers) {
        if (!isObject(container) || !isObject(container.inner_size) || !Array.isArray(container.placements)) {
            throw new Error('输出 JSON 的容器数据不完整')
        }
    }

    return value as unknown as ResultData
}

