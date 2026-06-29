import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { ClerkProvider } from '@clerk/react'
import './index.css'
import AppRoutes from './AppRoutes.jsx'
import { SIGN_IN_REDIRECT, SIGN_OUT_REDIRECT, SIGN_UP_COMPLETE_REDIRECT } from './lib/authRedirects.js'

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <ClerkProvider
      publishableKey={import.meta.env.VITE_CLERK_PUBLISHABLE_KEY}
      afterSignOutUrl={SIGN_OUT_REDIRECT}
      signInFallbackRedirectUrl={SIGN_IN_REDIRECT}
      signUpFallbackRedirectUrl={SIGN_UP_COMPLETE_REDIRECT}
      signUpForceRedirectUrl={SIGN_UP_COMPLETE_REDIRECT}
    >
      <AppRoutes />
    </ClerkProvider>
  </StrictMode>,
)

// Register the PWA service worker so the app is installable on iOS & Android.
if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(() => {})
  })
}
