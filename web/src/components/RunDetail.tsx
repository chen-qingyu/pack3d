import { lazy, Suspense } from 'react'
import { Activity, Archive, ArrowLeft, Box, Check, CircleAlert, Download, FileJson, Gauge, LayoutGrid, LoaderCircle, RefreshCw, Square, Trash2 } from 'lucide-react'
import { ActionIcon, Alert, Badge, Button, Container, Divider, Group, Paper, SimpleGrid, Stack, Table, Text, ThemeIcon, Title } from '@mantine/core'
import type { ContainerResult, ResultData, Run } from '../api'
import { api } from '../api'
import { formatDate, formatPercent, statusLabel } from '../format'
import { Metric, StatusBadge } from './Ui'

const Viewer3D = lazy(() => import('../Viewer3D'))

type RunDetailProps = {
  run: Run
  result: ResultData | null
  onBack: () => void
  onCancel: () => void
  onDelete: () => void
  onNewRun: () => void
  onRename: () => void
}

export function RunDetail({ run, result, onBack, onCancel, onDelete, onNewRun, onRename }: RunDetailProps) {
  const summary = result?.summary || run.summary
  const containers = result?.result?.containers || []
  const boxTypes = result?.result?.box_types || []
  const violations = result?.violations || run.violations || []
  const statusColor = run.status === 'completed' ? 'teal' : run.status === 'running' ? 'orange' : run.status === 'invalid' || run.status === 'failed' ? 'red' : 'gray'
  return <Container size="xl" py="xl">
    <Group justify="space-between" align="flex-end" mb="xl"><div><Button variant="subtle" px={0} leftSection={<ArrowLeft size={16} />} onClick={onBack}>返回实例</Button><Group gap="xs"><ThemeIcon variant="light" size="sm"><Activity size={14} /></ThemeIcon><Text size="sm" c="teal" fw={600}>运行详情</Text></Group><Title mt="xs">{run.run_name}</Title><Text size="sm" c="dimmed" mt={4}>{run.run_id} · {formatDate(run.created_at)}</Text></div><Group>{run.status === 'running' ? <Button color="red" variant="light" leftSection={<Square size={14} />} onClick={onCancel}>取消运行</Button> : <Button variant="default" leftSection={<RefreshCw size={16} />} onClick={onNewRun}>再次运行</Button>}<Button variant="default" onClick={onRename}>重命名</Button><ActionIcon color="red" variant="subtle" size="lg" onClick={onDelete}><Trash2 size={16} /></ActionIcon></Group></Group>
    <Paper withBorder p="md" mb="lg"><Group><ThemeIcon color={statusColor} variant="light" size="lg">{run.status === 'running' ? <LoaderCircle size={20} /> : run.status === 'completed' ? <Check size={20} /> : <CircleAlert size={20} />}</ThemeIcon><div style={{ flex: 1 }}><Text fw={600}>{statusLabel[run.status]}</Text><Text size="sm" c="dimmed">{run.status === 'running' ? '求解器正在计算中，页面会自动更新。' : run.error || (run.status === 'invalid' ? '引擎返回了输入校验问题。' : '本次运行已结束。')}</Text></div><StatusBadge status={run.status} /></Group></Paper>
    {summary && <SimpleGrid cols={{ base: 1, sm: 2, lg: 4 }} mb="lg">
      <Metric label="平均体积率" value={formatPercent(summary.volume_rate)} icon={<Gauge size={17} />} accent />
      <Metric label="使用容器" value={String(summary.container_count)} icon={<Archive size={17} />} />
      <Metric label="已装 / 未装箱" value={`${summary.packed_box_count} / ${summary.unpacked_box_count}`} icon={<Box size={17} />} />
      <Metric label="平台分割数" value={String(summary.platform_split)} icon={<LayoutGrid size={17} />} />
    </SimpleGrid>}
    <ContainerTable containers={containers} /><Suspense fallback={<Paper withBorder mt="lg" p="md"><Group justify="center"><LoaderCircle size={18} /><Text size="sm" c="dimmed">正在加载三维视图</Text></Group></Paper>}><Viewer3D containers={containers} boxTypes={boxTypes} /></Suspense>
    {violations.length > 0 && <Alert color="red" title="违规信息" mt="lg"><Group gap="xs">{violations.map((violation, index) => <Badge key={`${index}-${violation}`} color="red" variant="light">{violation}</Badge>)}</Group></Alert>}
    {result && <Paper withBorder p="md" mt="lg"><Group mb="sm"><FileJson size={16} /><Text fw={600}>原始输出 JSON</Text></Group><Divider mb="sm" /><Text component="pre" size="xs" style={{ overflow: 'auto', maxHeight: 400 }}>{JSON.stringify(result, null, 2)}</Text></Paper>}
    <Button component="a" variant="subtle" mt="md" leftSection={<Download size={14} />} href={api.downloadResult(run.instance_id, run.run_id)} download target="_blank" rel="noopener noreferrer">下载原始输出 JSON</Button>
  </Container>
}

function ContainerTable({ containers }: { containers: ContainerResult[] }) {
  return <Paper withBorder mt="lg"><Stack gap={0}><div><Title order={3} p="md">容器明细</Title><Text size="sm" c="dimmed" px="md" pb="md">按装车顺序排列</Text></div><Table.ScrollContainer minWidth={850}><Table striped><Table.Thead><Table.Tr><Table.Th>容器</Table.Th><Table.Th>尺寸</Table.Th><Table.Th>体积率</Table.Th><Table.Th>重量率</Table.Th><Table.Th>装载</Table.Th><Table.Th>平台 / 分组</Table.Th></Table.Tr></Table.Thead><Table.Tbody>{containers.map((container, index) => <Table.Tr key={`${container.type_id}-${index}`}><Table.Td><Text fw={600}>#{index + 1} · {container.type_id}</Text></Table.Td><Table.Td>{container.inner_size.x} × {container.inner_size.y} × {container.inner_size.z}</Table.Td><Table.Td>{formatPercent(container.volume_rate)}</Table.Td><Table.Td>{formatPercent(container.weight_rate)}</Table.Td><Table.Td>{container.packed_count} 箱</Table.Td><Table.Td>{container.platforms?.join(', ') || '无平台'} / {container.groups?.join(', ') || '无分组'}</Table.Td></Table.Tr>)}</Table.Tbody></Table></Table.ScrollContainer></Stack></Paper>
}
