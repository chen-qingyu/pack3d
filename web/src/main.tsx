import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { MantineProvider } from '@mantine/core'
import '@mantine/core/styles.css'
import App from './App.tsx'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <MantineProvider theme={{ primaryColor: 'teal', fontFamily: 'Manrope, Segoe UI, sans-serif' }} defaultColorScheme="light">
      <App />
    </MantineProvider>
  </StrictMode>,
)
