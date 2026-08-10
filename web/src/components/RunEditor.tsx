import { ArrowLeft, Check, FileJson, Gauge, Play, RefreshCw, Upload } from 'lucide-react'
import { ActionIcon, Button, Container, Grid, Group, NumberInput, Paper, Select, Stack, Text, TextInput, Textarea, ThemeIcon, Title } from '@mantine/core'
import { initialInput, type RunFormState } from '../format'

type RunEditorProps = {
  form: RunFormState
  setField: <K extends keyof RunFormState>(field: K, value: RunFormState[K]) => void
  onBack: () => void
  onRun: () => void
  onFile: (file: File) => void
  onFormat: () => void
}

export function RunEditor({ form, setField, onBack, onRun, onFile, onFormat }: RunEditorProps) {
  return <Container size="xl" py="xl">
    <Group justify="space-between" align="flex-end" mb="xl">
      <div>
        <Button variant="subtle" px={0} leftSection={<ArrowLeft size={16} />} onClick={onBack}>返回实例</Button>
        <Group gap="xs"><ThemeIcon variant="light" size="sm"><FileJson size={14} /></ThemeIcon><Text size="sm" c="teal" fw={600}>新建运行</Text></Group>
        <Title mt="xs">装载输入</Title>
        <Text size="sm" c="dimmed" mt={4}>输入 JSON 由引擎校验语法与业务规则。</Text>
      </div>
      <Group>
        <Button variant="default" leftSection={<Check size={16} />} onClick={onFormat}>格式化</Button>
        <Button component="label" variant="default" leftSection={<Upload size={16} />}>导入 JSON
          <input hidden type="file" accept="application/json,.json" onChange={(event) => { const file = event.target.files?.[0]; if (file) onFile(file); event.currentTarget.value = '' }} />
        </Button>
        <Button leftSection={<Play size={16} />} onClick={onRun}>开始运行</Button>
      </Group>
    </Group>
    <Grid>
      <Grid.Col span={{ base: 12, md: 8 }}>
        <Paper withBorder p="md">
          <Group justify="space-between" mb="sm">
            <div><Title order={3}>输入 JSON</Title><Text size="sm" c="dimmed">引擎会校验 JSON 结构与业务规则。</Text></div>
            <ActionIcon variant="subtle" onClick={() => setField('inputText', initialInput)}><RefreshCw size={16} /></ActionIcon>
          </Group>
          <Textarea autosize minRows={24} value={form.inputText} onChange={(event) => setField('inputText', event.target.value)} spellCheck={false} styles={{ input: { fontFamily: 'monospace' } }} />
        </Paper>
      </Grid.Col>
      <Grid.Col span={{ base: 12, md: 4 }}>
        <Paper withBorder p="md">
          <Stack>
            <Group justify="space-between">
              <div><Title order={3}>运行配置</Title><Text size="sm" c="dimmed">求解器参数</Text></div>
              <Gauge size={20} />
            </Group>
            <TextInput label="运行名称" value={form.runName} onChange={(event) => setField('runName', event.target.value)} />
            <Select label="算法" data={[
              { value: '', label: '默认' },
              { value: 'gep', label: 'GEP' },
              { value: 'glc', label: 'GLC' },
              { value: 'rgs', label: 'RGS' },
              { value: 'bsg', label: 'BSG' },
            ]} value={form.algorithm} onChange={(v) => setField('algorithm', v)} clearable />
            <NumberInput label="时限 (秒)" value={form.timeLimit} onChange={(v) => setField('timeLimit', v)} min={0} step={1} decimalScale={1} />
            <NumberInput label="支撑率 (0~1)" value={form.supportRate} onChange={(v) => setField('supportRate', v)} min={0} max={1} step={0.01} decimalScale={2} />
            <NumberInput label="平台数量限制" value={form.platformLimit} onChange={(v) => setField('platformLimit', v)} min={0} step={1} />
            <NumberInput label="标书数量限制" value={form.tenderLimit} onChange={(v) => setField('tenderLimit', v)} min={0} step={1} />
          </Stack>
        </Paper>
      </Grid.Col>
    </Grid>
  </Container>
}
