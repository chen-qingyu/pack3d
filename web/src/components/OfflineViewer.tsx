import { lazy, Suspense } from 'react'
import { Button, Container, Group, Paper, SimpleGrid, Stack, Text, Title } from '@mantine/core'
import { Archive, Box as BoxIcon, FileJson, Gauge, LoaderCircle, Upload } from 'lucide-react'
import type { ResultData } from '../api'
import { formatPercent } from '../format'
import { Metric } from './Ui'

const Viewer3D = lazy(() => import('../Viewer3D'))

type OfflineViewerProps = {
    fileName: string
    result: ResultData | null
    onFile: (file: File) => void
}

export function OfflineViewer({ fileName, result, onFile }: OfflineViewerProps) {
    const containers = result?.result?.containers ?? []
    const boxTypes = result?.result?.box_types ?? []
    const summary = result?.summary

    return <Container size="xl" py="xl">
        <Group justify="space-between" align="flex-end" mb="xl">
            <div>
                <Group gap="xs"><FileJson size={16} /><Text size="sm" c="teal" fw={600}>离线结果</Text></Group>
                <Title mt="xs">装箱结果查看</Title>
                <Text size="sm" c="dimmed" mt={4}>{fileName || '选择 pack3d 输出 JSON'}</Text>
            </div>
            <Button component="label" leftSection={<Upload size={16} />}>打开 JSON<input hidden type="file" accept="application/json,.json" onChange={(event) => { const file = event.target.files?.[0]; if (file) onFile(file); event.currentTarget.value = '' }} /></Button>
        </Group>
        {!result && <Paper withBorder p="xl"><Stack align="center" gap="sm"><FileJson size={28} /><Text fw={600}>尚未加载结果</Text><Button component="label" variant="default" leftSection={<Upload size={16} />}>选择输出 JSON<input hidden type="file" accept="application/json,.json" onChange={(event) => { const file = event.target.files?.[0]; if (file) onFile(file); event.currentTarget.value = '' }} /></Button></Stack></Paper>}
        {result && <>
            {summary && <SimpleGrid cols={{ base: 1, sm: 2, lg: 4 }} mb="lg">
                <Metric label="平均体积率" value={formatPercent(summary.volume_rate)} icon={<Gauge size={17} />} accent />
                <Metric label="使用容器" value={String(summary.container_count)} icon={<Archive size={17} />} />
                <Metric label="已装 / 未装箱" value={`${summary.packed_box_count} / ${summary.unpacked_box_count}`} icon={<BoxIcon size={17} />} />
                <Metric label="结果状态" value={result.status} icon={<FileJson size={17} />} />
            </SimpleGrid>}
            <Suspense fallback={<Paper withBorder p="md"><Group justify="center"><LoaderCircle size={18} /><Text size="sm" c="dimmed">正在加载三维视图</Text></Group></Paper>}><Viewer3D containers={containers} boxTypes={boxTypes} /></Suspense>
            <Paper withBorder p="md" mt="lg"><Text component="pre" size="xs" style={{ overflow: 'auto', maxHeight: 300 }}>{JSON.stringify(result, null, 2)}</Text></Paper>
        </>}
    </Container>
}