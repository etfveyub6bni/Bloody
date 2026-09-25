import { getStore } from "@netlify/blobs";

const ADMIN_PASSWORD = process.env.BLOODY_ADMIN_PASSWORD;
const STORE_NAME = "bloody-auth";
const STATE_KEY = "state";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "Content-Type,X-Admin-Password",
  "Access-Control-Allow-Methods": "GET,POST,OPTIONS"
};

export const handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return response(204, "");
  }

  try {
    const path = routePath(event);
    if (path === "/validate" && event.httpMethod === "POST") {
      return validate(event);
    }
    if (path === "/admin-state" && event.httpMethod === "POST") {
      requireAdmin(event);
      return response(200, await adminState());
    }
    if (path === "/generate-key" && event.httpMethod === "POST") {
      requireAdmin(event);
      return response(200, await generateKey());
    }
    if (path === "/unbind" && event.httpMethod === "POST") {
      requireAdmin(event);
      return response(200, await unbind(event));
    }

    return response(404, { ok: false, error: "not found" });
  } catch (error) {
    const status = error.status || 500;
    return response(status, { ok: false, error: error.message || "server error" });
  }
};

async function validate(event) {
  const body = parseJson(event.body);
  const keyValue = normalizeKey(body.key);
  const hwid = String(body.hwid || "").trim();

  if (keyValue.length < 12 || !hwid) {
    return response(400, { ok: false, status: "invalid_key", message: "invalid key" });
  }

  const state = await readState();
  const key = state.keys.find((record) => record.value === keyValue);
  if (!key) {
    return response(404, { ok: false, status: "invalid_key", message: "invalid key" });
  }

  if (key.status === "revoked") {
    return response(403, { ok: false, status: "key_unbound", message: "ключ отвязан" });
  }

  if (key.hwid && key.hwid !== hwid) {
    return response(403, { ok: false, status: "wrong_hwid", message: "wrong HWID" });
  }

  const now = new Date().toISOString();
  if (!key.hwid) {
    key.hwid = hwid;
    key.boundAt = now;
  }
  key.lastSeenAt = now;
  key.status = "active";
  await writeState(state);

  return response(200, { ok: true, status: "success", message: "successfully", key });
}

async function adminState() {
  const state = await readState();
  return { ok: true, keys: state.keys };
}

async function generateKey() {
  const state = await readState();
  let value;
  do {
    value = randomKey();
  } while (state.keys.some((key) => key.value === value));

  const key = freshKey(value);
  state.keys.unshift(key);
  await writeState(state);
  return { ok: true, key: value, keys: state.keys };
}

async function unbind(event) {
  const body = parseJson(event.body);
  const keyValue = normalizeKey(body.key);
  const state = await readState();
  const key = state.keys.find((record) => record.value === keyValue);
  if (!key) {
    return { ok: false, status: "invalid_key", message: "invalid key" };
  }

  key.status = "active";
  key.hwid = "";
  key.boundAt = "";
  key.lastSeenAt = "";
  key.revokedAt = new Date().toISOString();
  await writeState(state);
  return { ok: true, status: "key_unbound", keys: state.keys };
}

async function readState() {
  const store = getStore(STORE_NAME);
  const existing = await store.get(STATE_KEY, { type: "json" });
  if (existing && Array.isArray(existing.keys)) {
    return existing;
  }

  const state = {
    keys: []
  };
  await writeState(state);
  return state;
}

async function writeState(state) {
  const store = getStore(STORE_NAME);
  await store.setJSON(STATE_KEY, state);
}

function freshKey(value) {
  return {
    value,
    status: "active",
    createdAt: new Date().toISOString(),
    hwid: "",
    boundAt: "",
    lastSeenAt: "",
    revokedAt: ""
  };
}

function requireAdmin(event) {
  const password = event.headers["x-admin-password"] || event.headers["X-Admin-Password"];
  if (!ADMIN_PASSWORD) {
    const error = new Error("admin password is not configured");
    error.status = 503;
    throw error;
  }
  if (password !== ADMIN_PASSWORD) {
    const error = new Error("invalid password");
    error.status = 401;
    throw error;
  }
}

function routePath(event) {
  const raw = event.path || "";
  return raw
    .replace(/^\/\.netlify\/functions\/api/, "")
    .replace(/^\/api/, "")
    || "/";
}

function parseJson(body) {
  if (!body) {
    return {};
  }
  try {
    return JSON.parse(body);
  } catch {
    return {};
  }
}

function normalizeKey(value) {
  return String(value || "").replace(/\D/g, "");
}

function randomKey() {
  let value = "";
  for (let index = 0; index < 12; index += 1) {
    value += Math.floor(Math.random() * 10);
  }
  return value;
}

function response(statusCode, payload) {
  const isText = typeof payload === "string";
  return {
    statusCode,
    headers: {
      ...corsHeaders,
      "Content-Type": isText ? "text/plain; charset=utf-8" : "application/json; charset=utf-8"
    },
    body: isText ? payload : JSON.stringify(payload)
  };
}
