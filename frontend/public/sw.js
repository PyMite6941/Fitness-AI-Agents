// Minimal service worker — makes the app installable (PWA) on iOS & Android and
// serves a cached shell when offline. Network-first so fresh content wins online.
const CACHE = 'fitnessai-v1';
const SHELL = ['/', '/index.html', '/manifest.webmanifest', '/favicon.svg'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

// ── Web push: show the daily Readiness/Watchdog notification ─────────────────
function parsePushData(d) {
  if (!d) return {};
  try { return d.json(); } catch { return { body: d.text() }; }
}

self.addEventListener('push', (e) => {
  const data = parsePushData(e.data);
  const title = data.title || 'FitnessAI';
  const options = {
    body: data.body || '',
    icon: '/icon-192.png',
    badge: '/icon-192.png',
    data: { url: data.url || '/coach' },
    tag: 'daily-insights',        // collapse repeats into one
    renotify: true,
  };
  e.waitUntil(self.registration.showNotification(title, options));
});

// Focus an existing tab or open the app when the notification is tapped.
self.addEventListener('notificationclick', (e) => {
  e.notification.close();
  const url = (e.notification.data && e.notification.data.url) || '/coach';
  e.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((wins) => {
      for (const w of wins) {
        if ('focus' in w) { w.navigate(url); return w.focus(); }
      }
      return self.clients.openWindow(url);
    })
  );
});

self.addEventListener('fetch', (e) => {
  const { request } = e;
  if (request.method !== 'GET') return;
  // Never cache API calls — always hit the network.
  if (/\/(analyze|ingest|user|charts|routes|device|integrations|demo|watch)\b/.test(new URL(request.url).pathname)) return;
  e.respondWith(
    fetch(request)
      .then((res) => {
        const copy = res.clone();
        caches.open(CACHE).then((c) => c.put(request, copy)).catch(() => {});
        return res;
      })
      .catch(() => caches.match(request).then((m) => m || caches.match('/index.html')))
  );
});
