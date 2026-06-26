import { StrictMode, useEffect, useRef } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Routes, Route, Navigate, useLocation, useNavigate } from 'react-router-dom'
import { ClerkProvider, useAuth } from '@clerk/react'
import './index.css'
import App from './App.jsx'
import Dashboard from './pages/Dashboard.jsx'
import RoutesPage from './pages/Routes.jsx'
import LogWorkout from './pages/LogWorkout.jsx'
import DemoDashboard from './pages/DemoDashboard.jsx'
import DownloadApp from './pages/DownloadApp.jsx'
import Coach from './pages/Coach.jsx'
import Chat from './pages/Chat.jsx'
import Privacy from './pages/Privacy.jsx'
import Devices from './pages/Devices.jsx'
import { captureEvent, identifyUser, initAnalytics } from './lib/analytics.js'

function ProtectedRoute({ children }) {
  const { isSignedIn, isLoaded } = useAuth()
  if (!isLoaded) return null
  if (!isSignedIn) return <Navigate to="/" replace />
  return children
}

function AnalyticsTracker() {
  const { isLoaded, isSignedIn, userId } = useAuth()
  const location = useLocation()
  const navigate = useNavigate()
  const signupCompletionTracked = useRef(false)

  useEffect(() => {
    initAnalytics()
  }, [])

  useEffect(() => {
    captureEvent('page_viewed', {
      route: location.pathname,
      has_query: Boolean(location.search),
    })

    if (location.pathname === '/') {
      captureEvent('homepage_viewed', {
        route: '/',
      })
    }
  }, [location.pathname, location.search])

  useEffect(() => {
    if (!isLoaded || !isSignedIn || !userId) return
    identifyUser(userId)
  }, [isLoaded, isSignedIn, userId])

  useEffect(() => {
    if (!isLoaded || !isSignedIn || signupCompletionTracked.current) return

    const params = new URLSearchParams(location.search)
    if (params.get('signup') !== 'complete') return

    signupCompletionTracked.current = true
    captureEvent('signup_completed', {
      route: location.pathname,
    })

    params.delete('signup')
    navigate({
      pathname: location.pathname,
      search: params.toString() ? `?${params.toString()}` : '',
      hash: location.hash,
    }, { replace: true })
  }, [isLoaded, isSignedIn, location.hash, location.pathname, location.search, navigate])

  return null
}

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <ClerkProvider
      publishableKey={import.meta.env.VITE_CLERK_PUBLISHABLE_KEY}
      afterSignOutUrl="/" afterSignInUrl="/dashboard" afterSignUpUrl="/dashboard?signup=complete"
    >
      <BrowserRouter>
        <AnalyticsTracker />
        <Routes>
          <Route path="/" element={<App />} />
          <Route path="/demo" element={<DemoDashboard />} />
          <Route path="/app" element={<DownloadApp />} />
          <Route path="/privacy" element={<Privacy />} />
          <Route path="/dashboard" element={<ProtectedRoute><Dashboard /></ProtectedRoute>} />
          <Route path="/routes" element={<ProtectedRoute><RoutesPage /></ProtectedRoute>} />
          <Route path="/log" element={<ProtectedRoute><LogWorkout /></ProtectedRoute>} />
          <Route path="/coach" element={<ProtectedRoute><Coach /></ProtectedRoute>} />
          <Route path="/chat" element={<ProtectedRoute><Chat /></ProtectedRoute>} />
          <Route path="/devices" element={<ProtectedRoute><Devices /></ProtectedRoute>} />
        </Routes>
      </BrowserRouter>
    </ClerkProvider>
  </StrictMode>,
)

// Register the PWA service worker so the app is installable on iOS & Android.
if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(() => {})
  })
}
