import { useEffect, useRef, type Dispatch, type SetStateAction } from 'react'
import { api } from '../api'
import type { ResultData, Run } from '../api'

type UseRunPollingOptions = {
  activeRun: Run | null
  setActiveRun: Dispatch<SetStateAction<Run | null>>
  setResult: Dispatch<SetStateAction<ResultData | null>>
  refreshInstances: () => Promise<void>
  setError: (message: string) => void
}

export function useRunPolling({ activeRun, setActiveRun, setResult, refreshInstances, setError }: UseRunPollingOptions) {
  const activeRunRef = useRef<Run | null>(activeRun)
  activeRunRef.current = activeRun

  useEffect(() => {
    if (!activeRunRef.current || activeRunRef.current.status !== 'running') return
    let cancelled = false
    let timer: number | undefined
    let step = 0
    const poll = async () => {
      try {
        const currentRun = activeRunRef.current
        if (!currentRun) return
        const next = await api.getRun(currentRun.instance_id, currentRun.run_id)
        if (cancelled) return
        if (next.status === 'running') {
          setActiveRun(next)
          const delays = [200, 500, 1000, 2000, 4000, 5000]
          const delay = document.visibilityState === 'hidden' ? 15000 : delays[Math.min(step++, delays.length - 1)]
          timer = window.setTimeout(poll, delay)
        } else if (next.status === 'completed' || next.status === 'invalid') {
          const nextResult = await api.getResult(next.instance_id, next.run_id)
          if (cancelled) return
          setResult(nextResult)
          setActiveRun(next)
          await refreshInstances()
        } else {
          setActiveRun(next)
        }
      } catch (reason) {
        if (!cancelled) setError(reason instanceof Error ? reason.message : '状态读取失败')
        timer = window.setTimeout(poll, document.visibilityState === 'hidden' ? 15000 : 5000)
      }
    }
    const onVisibility = () => { if (document.visibilityState === 'visible') void poll() }
    document.addEventListener('visibilitychange', onVisibility)
    void poll()
    return () => {
      cancelled = true
      if (timer) window.clearTimeout(timer)
      document.removeEventListener('visibilitychange', onVisibility)
    }
  }, [activeRun?.instance_id, activeRun?.run_id, activeRun?.status, refreshInstances, setActiveRun, setError, setResult])
}
