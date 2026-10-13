// Retired. Earlier versions of the console registered a caching service worker.
// This stub removes it and its caches so nobody keeps seeing a stale console.
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (event) => {
  event.waitUntil((async () => {
    for (const name of await caches.keys()) await caches.delete(name);
    await self.registration.unregister();
    for (const client of await self.clients.matchAll()) client.navigate(client.url);
  })());
});
