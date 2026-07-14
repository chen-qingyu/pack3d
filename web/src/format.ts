export const initialInput = `{
  "container_types": [],
  "box_types": [],
  "boxes": []
}`

export const statusLabel: Record<string, string> = {
  running: '运行中',
  completed: '已完成',
  invalid: '输入无效',
  failed: '失败',
  cancelled: '已取消',
}

export const formatDate = (value: string) => new Intl.DateTimeFormat('zh-CN', {
  month: 'short',
  day: 'numeric',
  hour: '2-digit',
  minute: '2-digit',
}).format(new Date(value))

export const formatPercent = (value: number | null | undefined) => value == null ? '—' : `${(value * 100).toFixed(1)}%`