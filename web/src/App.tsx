import { useCallback, useEffect, useState } from 'react'
import { AppShell, Box, Button, Divider, Group, Loader, NavLink, Stack, Text, TextInput, ThemeIcon, Tooltip } from '@mantine/core'
import { Archive, ChevronRight, FileJson, Package, Plus, Search, Server, Upload } from 'lucide-react'
import { api } from './api'
import type { Instance, ResultData, Run } from './api'
import { EmptyInstances, InstanceOverview } from './components/InstanceOverview'
import { OfflineViewer } from './components/OfflineViewer'
import { RunDetail } from './components/RunDetail'
import { RunEditor } from './components/RunEditor'
import { initialInput } from './format'
import { useRunPolling } from './hooks/useRunPolling'
import { parseOfflineResult } from './offline'

type View = 'instances' | 'new-run' | 'run' | 'offline'

function App() {
  const [instances, setInstances] = useState<Instance[]>([])
  const [selectedInstance, setSelectedInstance] = useState<Instance | null>(null)
  const [activeRun, setActiveRun] = useState<Run | null>(null)
  const [result, setResult] = useState<ResultData | null>(null)
  const [offlineResult, setOfflineResult] = useState<ResultData | null>(null)
  const [offlineFileName, setOfflineFileName] = useState('')
  const [view, setView] = useState<View>('instances')
  const [inputText, setInputText] = useState(initialInput)
  const [inputError, setInputError] = useState('')
  const [loading, setLoading] = useState(true)
  const [apiOnline, setApiOnline] = useState(true)
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')
  const [instanceName, setInstanceName] = useState('')
  const [runName, setRunName] = useState('')
  const [randomSeed, setRandomSeed] = useState('42')
  const [algorithm, setAlgorithm] = useState<string | null>(null)
  const [timeLimit, setTimeLimit] = useState<string | number>('')
  const [supportRate, setSupportRate] = useState<string | number>('')
  const [platformLimit, setPlatformLimit] = useState<string | number>('')
  const [tenderLimit, setTenderLimit] = useState<string | number>('')
  const [search, setSearch] = useState('')

  const refreshInstances = useCallback(async () => {
    const response = await api.listInstances()
    setApiOnline(true)
    setInstances(response.instances)
    if (selectedInstance) setSelectedInstance(await api.getInstance(selectedInstance.instance_id))
  }, [selectedInstance])

  useEffect(() => {
    api.listInstances().then((response) => {
      setApiOnline(true)
      setInstances(response.instances)
      if (response.instances[0]) setSelectedInstance(response.instances[0])
    }).catch((reason: Error) => {
      setApiOnline(false)
      console.warn('pack3d API is offline:', reason.message)
    }).finally(() => setLoading(false))
  }, [])

  useRunPolling({ activeRun, setActiveRun, setResult, refreshInstances, setError })

  const filteredInstances = instances.filter((instance) => instance.instance_name.toLowerCase().includes(search.toLowerCase()))

  const createInstance = async () => {
    if (!instanceName.trim()) return
    try {
      const created = await api.createInstance(instanceName.trim())
      setInstances((current) => [created, ...current])
      setSelectedInstance(created)
      setInstanceName('')
      setNotice('实例已创建')
    } catch (reason) { setError(reason instanceof Error ? reason.message : '实例创建失败') }
  }

  const selectInstance = async (instance: Instance) => {
    try {
      setSelectedInstance(await api.getInstance(instance.instance_id))
      setView('instances')
    } catch (reason) { setError(reason instanceof Error ? reason.message : '实例读取失败') }
  }

  const deleteInstance = async (instance: Instance) => {
    if (!window.confirm(`确认删除"${instance.instance_name}"及其全部运行？`)) return
    try {
      await api.deleteInstance(instance.instance_id)
      const next = instances.filter((item) => item.instance_id !== instance.instance_id)
      setInstances(next)
      setSelectedInstance(next[0] || null)
      setView('instances')
      setNotice('实例已删除')
    } catch (reason) { setError(reason instanceof Error ? reason.message : '实例删除失败') }
  }

  const renameInstance = async () => {
    if (!selectedInstance) return
    const name = window.prompt('实例名称', selectedInstance.instance_name)
    if (!name?.trim()) return
    try {
      const updated = await api.renameInstance(selectedInstance.instance_id, name.trim())
      setSelectedInstance(updated)
      setInstances((current) => current.map((item) => item.instance_id === updated.instance_id ? updated : item))
    } catch (reason) { setError(reason instanceof Error ? reason.message : '重命名失败') }
  }

  const UI_OVERRIDE_KEYS = ['algorithm']
  const UI_CONSTRAINT_KEYS = ['time_limit', 'support_rate', 'platform_limit', 'tender_limit']

  const stripUiOverrides = (input: Record<string, unknown>) => {
    for (const key of UI_OVERRIDE_KEYS) delete input[key]
    const constraints = input.constraints as Record<string, unknown> | undefined
    if (constraints) {
      for (const key of UI_CONSTRAINT_KEYS) delete constraints[key]
      if (Object.keys(constraints).length === 0) delete input.constraints
    }
    return input
  }

  const openNewRun = async (instance: Instance, sourceRun?: Run) => {
    setSelectedInstance(instance)
    setRunName(instance.instance_name)
    setInputError('')
    setAlgorithm(null)
    setTimeLimit('')
    setSupportRate('')
    setPlatformLimit('')
    setTenderLimit('')
    try {
      const previous = sourceRun || instance.runs?.find((run) => run.status === 'completed')
      if (previous) {
        const raw = await api.getInput(instance.instance_id, previous.run_id)
        setInputText(JSON.stringify(stripUiOverrides(raw), null, 2))
      } else {
        setInputText(initialInput)
      }
    } catch { setInputText(initialInput) }
    setView('new-run')
  }

  const validateInput = (text: string): Record<string, unknown> | null => {
    try {
      const parsed = JSON.parse(text) as Record<string, unknown>
      setInputError('')
      return parsed
    } catch (reason) {
      setInputError(reason instanceof Error ? `JSON 语法错误：${reason.message}` : 'JSON 语法错误')
      return null
    }
  }

  const createRun = async () => {
    if (!selectedInstance) return
    const input = validateInput(inputText)
    if (!input) return

    if (algorithm) {
      if ('algorithm' in input) { setInputError('JSON 已指定 algorithm，不能同时通过界面覆盖。'); return }
      input.algorithm = algorithm
    }
    if (timeLimit !== '' && timeLimit !== null) {
      const secs = Number(timeLimit)
      if (secs > 0) {
        const constraints = (input.constraints || {}) as Record<string, unknown>
        if ('time_limit' in constraints) { setInputError('JSON 已指定 constraints.time_limit，不能同时通过界面覆盖。'); return }
        constraints.time_limit = secs
        input.constraints = constraints
      }
    }

    const constraintsInjection = (input.constraints || {}) as Record<string, unknown>
    let needConstraints = false

    if (supportRate !== '' && supportRate !== null) {
      const val = Number(supportRate)
      if (val > 0) {
        if ('support_rate' in constraintsInjection) { setInputError('JSON 已指定 constraints.support_rate，不能同时通过界面覆盖。'); return }
        constraintsInjection.support_rate = val
        needConstraints = true
      }
    }
    if (platformLimit !== '' && platformLimit !== null) {
      const val = Number(platformLimit)
      if (val >= 1) {
        if ('platform_limit' in constraintsInjection) { setInputError('JSON 已指定 constraints.platform_limit，不能同时通过界面覆盖。'); return }
        constraintsInjection.platform_limit = val
        needConstraints = true
      }
    }
    if (tenderLimit !== '' && tenderLimit !== null) {
      const val = Number(tenderLimit)
      if (val >= 1) {
        if ('tender_limit' in constraintsInjection) { setInputError('JSON 已指定 constraints.tender_limit，不能同时通过界面覆盖。'); return }
        constraintsInjection.tender_limit = val
        needConstraints = true
      }
    }
    if (needConstraints) {
      input.constraints = constraintsInjection
    }

    try {
      const created = await api.createRun(selectedInstance.instance_id, input, Number(randomSeed) || 42, runName.trim() || selectedInstance.instance_name)
      setActiveRun(created)
      setResult(null)
      setView('run')
      setNotice('运行已提交')
      await refreshInstances()
    } catch (reason) { setError(reason instanceof Error ? reason.message : '运行提交失败') }
  }

  const openRun = async (run: Run) => {
    setActiveRun(run)
    setResult(null)
    setView('run')
    if (run.status === 'completed' || run.status === 'invalid') {
      try { setResult(await api.getResult(run.instance_id, run.run_id)) } catch (reason) { setError(reason instanceof Error ? reason.message : '结果读取失败') }
    }
  }

  const cancelRun = async () => {
    if (!activeRun || activeRun.status !== 'running') return
    try { setActiveRun(await api.cancelRun(activeRun.instance_id, activeRun.run_id)); setNotice('运行已取消') } catch (reason) { setError(reason instanceof Error ? reason.message : '取消失败') }
  }

  const renameRun = async () => {
    if (!activeRun) return
    const name = window.prompt('运行名称', activeRun.run_name)
    if (!name?.trim()) return
    try {
      const updated = await api.renameRun(activeRun.instance_id, activeRun.run_id, name.trim())
      setActiveRun(updated)
      setSelectedInstance(await api.getInstance(updated.instance_id))
      setNotice('运行已重命名')
    } catch (reason) { setError(reason instanceof Error ? reason.message : '重命名失败') }
  }

  const deleteRun = async (run: Run) => {
    if (!selectedInstance || !window.confirm(`确认删除运行"${run.run_name}"？`)) return
    try {
      await api.deleteRun(run.instance_id, run.run_id)
      setSelectedInstance(await api.getInstance(run.instance_id))
      setView('instances')
      setNotice('运行已删除')
    } catch (reason) { setError(reason instanceof Error ? reason.message : '运行删除失败') }
  }

  const loadFile = (file: File) => {
    const reader = new FileReader()
    reader.onload = () => setInputText(String(reader.result || ''))
    reader.readAsText(file)
  }

  const loadOfflineFile = async (file: File) => {
    try {
      setOfflineResult(parseOfflineResult(await file.text()))
      setOfflineFileName(file.name)
      setError('')
      setView('offline')
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : '无法读取输出 JSON')
    }
  }

  if (loading) return <Stack align="center" justify="center" h="100vh"><Loader /><Text c="dimmed">正在连接 pack3d 服务</Text></Stack>

  return <AppShell header={{ height: 64 }} navbar={{ width: 280, breakpoint: 'sm' }} padding="lg">
    <AppShell.Header px="lg">
      <Group justify="space-between" h="100%">
        <Group gap="sm"><ThemeIcon size="lg" variant="light"><Package size={20} /></ThemeIcon><div><Text fw={700}>PACK3D</Text><Text size="xs" c="dimmed">装箱求解工作台</Text></div></Group>
        <Text size="sm" c={apiOnline ? 'teal' : 'red'}>API {apiOnline ? '在线' : '离线'} · REST / LOCAL</Text>
      </Group>
    </AppShell.Header>
    <AppShell.Navbar p="md">
      <Stack h="100%">
        <Group justify="space-between"><Text size="sm" fw={600}>实例</Text><Tooltip label="创建实例"><Button variant="subtle" size="compact-sm" onClick={() => document.getElementById('instance-name')?.focus()}><Plus size={16} /></Button></Tooltip></Group>
        <Button component="label" variant="light" leftSection={<Upload size={16} />}>离线打开 JSON<input hidden type="file" accept="application/json,.json" onChange={(event) => { const file = event.target.files?.[0]; if (file) void loadOfflineFile(file); event.currentTarget.value = '' }} /></Button>
        <NavLink active={view === 'offline'} label="离线查看" description={offlineFileName || '本地结果'} leftSection={<FileJson size={16} />} onClick={() => setView('offline')} />
        <TextInput leftSection={<Search size={15} />} value={search} onChange={(event) => setSearch(event.target.value)} placeholder="搜索实例" />
        <Stack gap={4} style={{ overflowY: 'auto' }}>{filteredInstances.map((instance) => <NavLink key={instance.instance_id} active={selectedInstance?.instance_id === instance.instance_id} label={instance.instance_name} description={`${instance.run_count} 次运行`} leftSection={<Archive size={16} />} rightSection={<ChevronRight size={15} />} onClick={() => void selectInstance(instance)} />)}{!filteredInstances.length && <Text size="sm" c="dimmed" ta="center" py="md">暂无实例</Text>}</Stack>
        <Group gap={0} mt="auto"><TextInput id="instance-name" value={instanceName} onChange={(event) => setInstanceName(event.target.value)} onKeyDown={(event) => event.key === 'Enter' && void createInstance()} placeholder="新实例名称" style={{ flex: 1 }} /><Button onClick={() => void createInstance()}><Plus size={16} /></Button></Group>
        <Divider /><Group gap="xs"><ThemeIcon variant="light" size="sm"><Server size={14} /></ThemeIcon><div><Text size="xs" fw={600}>求解服务</Text><Text size="xs" c="dimmed">本地服务 · v0.1.0</Text></div></Group>
      </Stack>
    </AppShell.Navbar>
    <AppShell.Main>
      <Stack gap="sm" mb="md">{error && <Box><Text c="red" size="sm">{error}</Text></Box>}{notice && <Box><Text c="teal" size="sm">{notice}</Text></Box>}</Stack>
      {view === 'offline' && <OfflineViewer fileName={offlineFileName} result={offlineResult} onFile={(file) => void loadOfflineFile(file)} />}
      {!selectedInstance && view !== 'offline' && <EmptyInstances onCreate={() => document.getElementById('instance-name')?.focus()} />}
      {selectedInstance && view === 'instances' && <InstanceOverview instance={selectedInstance} onNewRun={() => void openNewRun(selectedInstance)} onRename={() => void renameInstance()} onDelete={() => void deleteInstance(selectedInstance)} onOpenRun={(run) => void openRun(run)} onDeleteRun={(run) => void deleteRun(run)} />}
      {selectedInstance && view === 'new-run' && <RunEditor inputText={inputText} setInputText={setInputText} inputError={inputError} runName={runName} setRunName={setRunName} randomSeed={randomSeed} setRandomSeed={setRandomSeed} algorithm={algorithm} setAlgorithm={setAlgorithm} timeLimit={timeLimit} setTimeLimit={setTimeLimit} supportRate={supportRate} setSupportRate={setSupportRate} platformLimit={platformLimit} setPlatformLimit={setPlatformLimit} tenderLimit={tenderLimit} setTenderLimit={setTenderLimit} onBack={() => setView('instances')} onRun={() => void createRun()} onFile={loadFile} onFormat={() => { const parsed = validateInput(inputText); if (parsed) setInputText(JSON.stringify(parsed, null, 2)) }} />}
      {selectedInstance && view === 'run' && activeRun && <RunDetail run={activeRun} result={result} onBack={() => setView('instances')} onCancel={() => void cancelRun()} onDelete={() => void deleteRun(activeRun)} onNewRun={() => void openNewRun(selectedInstance, activeRun)} onRename={() => void renameRun()} />}
    </AppShell.Main>
  </AppShell>
}

export default App
