// Browser push-subscription helpers for the daily Readiness/Watchdog notifications.
// The service worker (/sw.js) is registered in main.jsx; here we subscribe the
// PushManager and register/unregister the subscription with the backend.
import { api } from './api';

export function pushSupported() {
  return (
    typeof window !== 'undefined' &&
    'serviceWorker' in navigator &&
    'PushManager' in window &&
    'Notification' in window
  );
}

function urlBase64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - (base64String.length % 4)) % 4);
  const base64 = (base64String + padding).replace(/-/g, '+').replace(/_/g, '/');
  const raw = atob(base64);
  const out = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) out[i] = raw.charCodeAt(i);
  return out;
}

async function ready() {
  // main.jsx already called register('/sw.js'); wait for it to be active.
  return navigator.serviceWorker.ready;
}

// True if this browser already has a live push subscription.
export async function isSubscribed() {
  if (!pushSupported()) return false;
  try {
    const reg = await ready();
    return !!(await reg.pushManager.getSubscription());
  } catch {
    return false;
  }
}

// Ask permission, subscribe, and register with the backend. Returns true on success.
export async function enablePush(getToken) {
  if (!pushSupported()) throw new Error('Push notifications are not supported in this browser.');

  const token = await getToken();
  const { key, configured } = await api.getVapidKey(token);
  if (!configured || !key) throw new Error('Push is not enabled on the server yet.');

  const permission = await Notification.requestPermission();
  if (permission !== 'granted') throw new Error('Notification permission was not granted.');

  const reg = await ready();
  let sub = await reg.pushManager.getSubscription();
  if (!sub) {
    sub = await reg.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: urlBase64ToUint8Array(key),
    });
  }
  await api.subscribePush(token, sub.toJSON());
  return true;
}

// Unsubscribe locally and tell the backend to forget it.
export async function disablePush(getToken) {
  if (!pushSupported()) return;
  const reg = await ready();
  const sub = await reg.pushManager.getSubscription();
  if (sub) {
    const endpoint = sub.endpoint;
    try { await sub.unsubscribe(); } catch { /* ignore */ }
    try {
      const token = await getToken();
      await api.unsubscribePush(token, endpoint);
    } catch { /* best effort */ }
  }
}
