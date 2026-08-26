import type { ReactNode } from 'react';
import { Badge, Group, Paper, Text, ThemeIcon } from '@mantine/core';
import { statusLabel } from '../format';

export function Metric({ label, value, icon, accent = false }: { label: string; value: string; icon: ReactNode; accent?: boolean }) {
  return (
    <Paper withBorder p="md" radius="md">
      <Group justify="space-between" align="flex-start">
        <Text size="sm" c="dimmed">
          {label}
        </Text>
        <ThemeIcon variant={accent ? 'light' : 'subtle'} color={accent ? 'teal' : 'gray'}>
          {icon}
        </ThemeIcon>
      </Group>
      <Text size="xl" fw={700} mt="sm">
        {value}
      </Text>
    </Paper>
  );
}

export function StatusBadge({ status }: { status: string }) {
  const color = status === 'completed' ? 'teal' : status === 'running' ? 'orange' : status === 'invalid' || status === 'failed' ? 'red' : 'gray';
  return (
    <Badge color={color} variant="light">
      {statusLabel[status] || status}
    </Badge>
  );
}
