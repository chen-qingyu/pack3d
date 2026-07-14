import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { MantineProvider } from '@mantine/core'
import '@mantine/core/styles.css'
import '@mantine/notifications/styles.css'
import { Notifications } from '@mantine/notifications'
import App from './App.tsx'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <MantineProvider theme={{ primaryColor: 'teal', fontFamily: 'Manrope, Segoe UI, sans-serif' }} defaultColorScheme="light">
      <Notifications position="top-right" />
      <App />
    </MantineProvider>
  </StrictMode>,
)
