// REST client for the node. Auth is an HttpOnly session cookie obtained by
// POST /api/auth/login; state-changing calls carry X-Bucks-CSRF: 1 and the
// browser adds Origin. The API token itself is never stored.

export class ApiError extends Error {
  constructor(message, { status = 0, kind = "http", retryAfter = 0, body = null } = {}) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.kind = kind; // "network" | "timeout" | "http"
    this.retryAfter = retryAfter;
    this.body = body;
  }
  get offline() { return this.kind === "network" || this.kind === "timeout"; }
}

const listeners = new Set();
/** Subscribe to session-expired events (any 401 outside the login call). */
export function onSessionExpired(fn) { listeners.add(fn); return () => listeners.delete(fn); }

export async function api(method, path, body, { timeout = 15000 } = {}) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeout);
  const headers = { Accept: "application/json" };
  const init = { method, headers, credentials: "same-origin", cache: "no-store", signal: ctl.signal };
  if (method !== "GET") {
    headers["X-Bucks-CSRF"] = "1";
    if (body !== undefined) {
      headers["Content-Type"] = "application/json";
      init.body = JSON.stringify(body);
    }
  }
  let res;
  try {
    res = await fetch(path, init);
  } catch (e) {
    throw new ApiError(
      e && e.name === "AbortError" ? "The node did not answer in time." : "Cannot reach the node.",
      { kind: e && e.name === "AbortError" ? "timeout" : "network" },
    );
  } finally {
    clearTimeout(timer);
  }
  let data = null;
  const text = await res.text();
  if (text) { try { data = JSON.parse(text); } catch { data = null; } }
  if (res.ok) return data ?? {};
  const msg = (data && (data.error || data.message)) || fallbackMessage(res.status);
  if (res.status === 401 && path !== "/api/auth/login") listeners.forEach((fn) => fn());
  throw new ApiError(msg, { status: res.status, retryAfter: Number(res.headers.get("Retry-After")) || 0, body: data });
}

function fallbackMessage(status) {
  switch (status) {
    case 401: return "Not signed in.";
    case 403: return "The node refused this request.";
    case 404: return "Not found.";
    case 415: return "The node rejected the request format.";
    case 429: return "Too many requests. Slow down for a moment.";
    case 503: return "The node is busy or the feature is unavailable.";
    default: return `The node returned an error (HTTP ${status}).`;
  }
}

export const get = (p, o) => api("GET", p, undefined, o);
export const post = (p, b = {}, o) => api("POST", p, b, o);
