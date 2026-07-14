import { Activity, Archive, Clock3, LayoutGrid, LoaderCircle, Play, Plus, Trash2 } from 'lucide-react'
import { ActionIcon, Button, Center, Container, Group, Paper, SimpleGrid, Stack, Table, Text, ThemeIcon, Title } from '@mantine/core'
import type { Instance, Run } from '../api'
import { formatDate, formatPercent, statusLabel } from '../format'
import { Metric, StatusBadge } from './Ui'

export function EmptyInstances({ onCreate }: { onCreate: () => void }) {
  return <Center h="calc(100vh - 64px)"><Stack align="center"><ThemeIcon size={64} radius="xl" variant="light"><LayoutGrid size={30} /></ThemeIcon><Title order={2}>开始一个装箱实例</Title><Text c="dimmed">创建实例后，加载输入并运行求解器。</Text><Button leftSection={<Plus size={17} />} onClick={onCreate}>创建第一个实例</Button></Stack></Center>
}

type InstanceOverviewProps = {
  instance: Instance
  onNewRun: () => void
  onRename: () => void
  onDelete: () => void
  onOpenRun: (run: Run) => void
  onDeleteRun: (run: Run) => void
}

export function InstanceOverview({ instance, onNewRun, onRename, onDelete, onOpenRun, onDeleteRun }: InstanceOverviewProps) {
  const runs = instance.runs || []
  const running = runs.filter((run) => run.status === 'running').length
  return <Container size="xl" py="xl">
    <Group justify="space-between" align="flex-end" mb="xl">
      <div><Group gap="xs"><ThemeIcon variant="light" size="sm"><Archive size={14} /></ThemeIcon><Text size="sm" c="teal" fw={600}>实例工作台</Text></Group><Title mt="xs">{instance.instance_name}</Title><Text size="sm" c="dimmed" mt={4}>创建于 {formatDate(instance.created_at)} · {instance.run_count} 次运行</Text></div>
      <Group><Button variant="default" onClick={onRename}>重命名</Button><Button color="red" variant="light" leftSection={<Trash2 size={16} />} onClick={onDelete}>删除</Button><Button leftSection={<Play size={16} />} onClick={onNewRun}>新建运行</Button></Group>
    </Group>
    <SimpleGrid cols={{ base: 1, sm: 3 }} mb="lg">
      <Metric label="运行总数" value={String(instance.run_count)} icon={<Activity size={17} />} />
      <Metric label="正在运行" value={String(running)} icon={<LoaderCircle size={17} />} accent={running > 0} />
      <Metric label="最近状态" value={runs[0] ? statusLabel[runs[0].status] : '尚未运行'} icon={<Clock3 size={17} />} />
    </SimpleGrid>
    <Paper withBorder radius="md"><Group justify="space-between" p="md"><div><Title order={3}>运行历史</Title><Text size="sm" c="dimmed">每次求解都会保存输入和输出结果。</Text></div><Text size="sm" c="dimmed">{runs.length} RUNS</Text></Group>{runs.length ? <Table.ScrollContainer minWidth={720}><Table striped highlightOnHover><Table.Thead><Table.Tr><Table.Th>运行名称</Table.Th><Table.Th>状态</Table.Th><Table.Th>创建时间</Table.Th><Table.Th>结果摘要</Table.Th><Table.Th /></Table.Tr></Table.Thead><Table.Tbody>{runs.map((run) => <Table.Tr key={run.run_id} onClick={() => onOpenRun(run)} style={{ cursor: 'pointer' }}><Table.Td><Text fw={600}>{run.run_name}</Text><Text size="xs" c="dimmed">{run.run_id}</Text></Table.Td><Table.Td><StatusBadge status={run.status} /></Table.Td><Table.Td>{formatDate(run.created_at)}</Table.Td><Table.Td>{run.summary ? `${run.summary.container_count} 容器 · ${formatPercent(run.summary.volume_rate)}` : '—'}</Table.Td><Table.Td><ActionIcon color="red" variant="subtle" onClick={(event) => { event.stopPropagation(); onDeleteRun(run) }}><Trash2 size={16} /></ActionIcon></Table.Td></Table.Tr>)}</Table.Tbody></Table></Table.ScrollContainer> : <Text c="dimmed" ta="center" p="xl">此实例暂无运行</Text>}</Paper>
  </Container>
}
