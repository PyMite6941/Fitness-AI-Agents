import { useEffect, useRef } from 'react'
import { BrowserRouter, Routes, Route, Navigate, useLocation, useNavigate } from 'react-router-dom'
import { useAuth } from '@clerk/react'
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
import Pricing from './pages/Pricing.jsx'
import FreeTier from './pages/FreeTier.jsx'
import UseCases from './pages/UseCases.jsx'
import {
  captureEvent,
  captureSignupCompleteOnce,
  identifyUser,
  initAnalytics,
} from './lib/analytics.js'

export function ProtectedRoute({ children }) {
  const { isSignedIn, isLoaded } = useAuth()
  if (!isLoaded) return null
  if (!isSignedIn) return <Navigate to="/" replace />
  return children
}

export function AnalyticsTracker() {
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
    if (!isLoaded || !isSignedIn || !userId || signupCompletionTracked.current) return

    const params = new URLSearchParams(location.search)
    const hasSignupCompleteParam = params.get('signup') === 'complete'

    if (!hasSignupCompleteParam) return

    signupCompletionTracked.current = true
    captureSignupCompleteOnce(userId, {
      route: location.pathname,
      completion_source: 'signup_redirect',
    })

    params.delete('signup')
    navigate({
      pathname: location.pathname,
      search: params.toString() ? `?${params.toString()}` : '',
      hash: location.hash,
    }, { replace: true })
  }, [isLoaded, isSignedIn, userId, location.hash, location.pathname, location.search, navigate])

  return null
}

export default function AppRoutes() {
  return (
    <BrowserRouter>
      <AnalyticsTracker />
      <Routes>
        <Route path="/" element={<App />} />
        <Route path="/demo" element={<DemoDashboard />} />
        <Route path="/app" element={<DownloadApp />} />
        <Route path="/privacy" element={<Privacy />} />
        <Route path="/pricing" element={<Pricing />} />
        <Route path="/free" element={<FreeTier />} />
        <Route path="/use-cases" element={<UseCases />} />
        <Route path="/dashboard" element={<ProtectedRoute><Dashboard /></ProtectedRoute>} />
        <Route path="/routes" element={<ProtectedRoute><RoutesPage /></ProtectedRoute>} />
        <Route path="/log" element={<ProtectedRoute><LogWorkout /></ProtectedRoute>} />
        <Route path="/coach" element={<ProtectedRoute><Coach /></ProtectedRoute>} />
        <Route path="/chat" element={<ProtectedRoute><Chat /></ProtectedRoute>} />
        <Route path="/devices" element={<ProtectedRoute><Devices /></ProtectedRoute>} />
      </Routes>
    </BrowserRouter>
  )
}
