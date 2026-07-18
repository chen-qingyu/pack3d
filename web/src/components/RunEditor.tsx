import { ArrowLeft, Check, CircleAlert, FileJson, Gauge, Play, RefreshCw, Upload } from 'lucide-react'
import { ActionIcon, Alert, Button, Container, Grid, Group, NumberInput, Paper, Select, Stack, Text, TextInput, Textarea, ThemeIcon, Title } from '@mantine/core'
import { initialInput } from '../format'

type RunEditorProps = {
  inputText: string
  setInputText: (value: string) => void
  inputError: string
  runName: string
  setRunName: (value: string) => void
  randomSeed: string
  setRandomSeed: (value: string) => void
  algorithm: string | null
  setAlgorithm: (value: string | null) => void
  timeLimit: string | number
  setTimeLimit: (value: string | number) => void
  supportRate: string | number
  setSupportRate: (value: string | number) => void
  platformLimit: string | number
  setPlatformLimit: (value: string | number) => void
  tenderLimit: string | number
  setTenderLimit: (value: string | number) => void
  onBack: () => void
  onRun: () => void
  onFile: (file: File) => void
  onFormat: () => void
}

export function RunEditor({ inputText, setInputText, inputError, runName, setRunName, randomSeed, setRandomSeed, algorithm, setAlgorithm, timeLimit, setTimeLimit, supportRate, setSupportRate, platformLimit, setPlatformLimit, tenderLimit, setTenderLimit, onBack, onRun, onFile, onFormat }: RunEditorProps) {
  return <Container size="xl" py="xl"><Group justify="space-between" align="flex-end" mb="xl"><div><Button variant="subtle" px={0} leftSection={<ArrowLeft size={16} />} onClick={onBack}>返回实例</Button><Group gap="xs"><ThemeIcon variant="light" size="sm"><FileJson size={14} /></ThemeIcon><Text size="sm" c="teal" fw={600}>新建运行</Text></Group><Title mt="xs">装载输入</Title><Text size="sm" c="dimmed" mt={4}>输入 JSON 由引擎校验语法与业务规则。</Text></div><Group><Button variant="default" leftSection={<Check size={16} />} onClick={onFormat}>格式化</Button><Button component="label" variant="default" leftSection={<Upload size={16} />}>导入 JSON<input hidden type="file" accept="application/json,.json" onChange={(event) => event.target.files?.[0] && onFile(event.target.files[0])} /></Button><Button leftSection={<Play size={16} />} onClick={onRun}>开始运行</Button></Group></Group><Grid><Grid.Col span={{ base: 12, md: 8 }}><Paper withBorder p="md"><Group justify="space-between" mb="sm"><div><Title order={3}>输入 JSON</Title><Text size="sm" c="dimmed">引擎会校验 JSON 结构与业务规则。</Text></div><ActionIcon variant="subtle" onClick={() => setInputText(initialInput)}><RefreshCw size={16} /></ActionIcon></Group><Textarea autosize minRows={24} value={inputText} onChange={(event) => setInputText(event.target.value)} spellCheck={false} styles={{ input: { fontFamily: 'monospace' } }} error={inputError || undefined} /></Paper></Grid.Col><Grid.Col span={{ base: 12, md: 4 }}><Paper withBorder p="md"><Stack><Group justify="space-between"><div><Title order={3}>运行配置</Title><Text size="sm" c="dimmed">求解器参数</Text></div><Gauge size={20} /></Group><TextInput label="运行名称" value={runName} onChange={(event) => setRunName(event.target.value)} /><TextInput label="随机种子" type="number" value={randomSeed} onChange={(event) => setRandomSeed(event.target.value)} /><Select label="算法" data={[
    { value: '', label: '默认' },
    { value: 'gep', label: 'GEP' },
    { value: 'glc', label: 'GLC' },
    { value: 'rgs', label: 'RGS' },
    { value: 'bsg', label: 'BSG' },
  ]} value={algorithm} onChange={setAlgorithm} clearable /><NumberInput label="时限 (秒)" value={timeLimit} onChange={setTimeLimit} min={0} step={1} decimalScale={1} /><NumberInput label="支撑率 (0~1)" value={supportRate} onChange={setSupportRate} min={0} max={1} step={0.01} decimalScale={2} /><NumberInput label="平台数量限制" value={platformLimit} onChange={setPlatformLimit} min={1} step={1} /><NumberInput label="标书数量限制" value={tenderLimit} onChange={setTenderLimit} min={1} step={1} /></Stack></Paper></Grid.Col></Grid>{inputError && <Alert color="red" icon={<CircleAlert size={16} />} mt="md">{inputError}</Alert>}</Container>
}
