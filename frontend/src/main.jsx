import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import { ClerkProvider, useAuth } from '@clerk/react'
import './index.css'
import App from './App.jsx'
import Dashboard from './pages/Dashboard.jsx'
import RoutesPage from './pages/Routes.jsx'
import LogWorkout from './pages/LogWorkout.jsx'

function ProtectedRoute({ children }) {
  const { isSignedIn, isLoaded } = useAuth()
  if (!isLoaded) return null
  if (!isSignedIn) return <Navigate to="/" replace />
  return children
}

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <ClerkProvider afterSignOutUrl="/" afterSignInUrl="/dashboard" afterSignUpUrl="/dashboard">
      <BrowserRouter>
        <Routes>
          <Route path="/" element={<App />} />
          <Route path="/dashboard" element={<ProtectedRoute><Dashboard /></ProtectedRoute>} />
          <Route path="/routes" element={<ProtectedRoute><RoutesPage /></ProtectedRoute>} />
          <Route path="/log" element={<ProtectedRoute><LogWorkout /></ProtectedRoute>} />
        </Routes>
      </BrowserRouter>
    </ClerkProvider>
  </StrictMode>,
)
