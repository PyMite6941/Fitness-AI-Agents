import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
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

function ProtectedRoute({ children }) {
  const { isSignedIn, isLoaded } = useAuth()
  if (!isLoaded) return null
  if (!isSignedIn) return <Navigate to="/" replace />
  return children
}

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <ClerkProvider
      publishableKey={import.meta.env.VITE_CLERK_PUBLISHABLE_KEY}
      afterSignOutUrl="/" afterSignInUrl="/dashboard" afterSignUpUrl="/dashboard"
    >
      <BrowserRouter>
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
