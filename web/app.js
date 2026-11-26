// dashboard.
"use strict";

const $ = (id) => document.getElementById(id);
const REFRESH_MS = 3000; //3sec
let shotUrl = null;

//use to handle error messages
async function errorText(res) {
  try {
    const body = await res.json();
    if (body && body.error) return body.error;
  } catch (_) { /* not JSON */ }
  return `Request failed (${res.status})`;
}

async function call(path, options = {}) {
  const res = await fetch(path, {
    credentials: "same-origin",
    ...options,
    headers: { "X-Requested-With": "GhostGate", ...(options.headers || {}) },
  });
  if (!res.ok) throw new Error(await errorText(res));
  return res;
}

// attain the progress bar for system usage visuals
function setMeter(name, value) {
  const known = typeof value === "number";
  $(`${name}-value`).textContent = known ? `${Math.round(value)} %` : "–";
  $(`${name}-bar`).style.width = known ? `${Math.min(100, Math.max(0, value))}%` : "0";
}

// main driving function of the dashboard
function renderStatus(s) {
  const state = $("target-state");
  const restart = $("restart");
  if (s.target) {
    $("target-name").textContent = s.target.name;
    state.textContent = s.target.running ? "Running" : "Dead";
    state.className = "pill " + (s.target.running ? "ok" : "bad");
    $("target-pids").textContent = s.target.running
      ? `PID ${s.target.pids.join(", ")}`
      : "The program is not running.";
    restart.hidden = !s.target.can_restart;
  } else {
    $("target-name").textContent = "No target";
    state.textContent = "–";
    state.className = "pill";
    $("target-pids").textContent = "Start GhostGate with --target-path to watch a program.";
    restart.hidden = true;
  }
  setMeter("cpu", s.cpu);
  setMeter("ram", s.ram);
  $("updated").textContent = `Updated ${new Date().toLocaleTimeString()}`;
}

//pings the pc for updates (polling)
async function refresh() {
  try {
    renderStatus(await (await call("/api/status")).json());
  } catch (e) {
    $("updated").textContent = `Offline: ${e.message}`;
  }
}

//restarting function
$("restart").addEventListener("click", async () => {
  const name = $("target-name").textContent;
  if (!confirm(`Restart ${name}? Any open session in it will be closed.`)) return;
  const button = $("restart");
  const msg = $("restart-msg");
  button.disabled = true;
  msg.textContent = "Restarting…";
  try {
    const body = await (await call("/api/restart", { method: "POST" })).json();
    msg.textContent = `Restarted. New PID ${body.pid}. Grab the screen to read its new ID.`; //currently for my problem has to change this later.
    refresh();
  } catch (e) {
    msg.textContent = e.message;
  } finally {
    button.disabled = false;
  }
});

//screenshoting function
$("grab").addEventListener("click", async () => {
  const button = $("grab");
  const msg = $("shot-msg");
  button.disabled = true;
  msg.textContent = "Capturing…";
  try {
    // according to the mobile it's accessed on.
    const width = Math.min(3840, Math.max(640, Math.round(window.innerWidth * (window.devicePixelRatio || 1))));
    const blob = await (await call(`/api/screenshot?width=${width}`)).blob();
    if (shotUrl) URL.revokeObjectURL(shotUrl);
    shotUrl = URL.createObjectURL(blob);
    $("shot").src = shotUrl;
    $("shot-link").href = shotUrl;
    $("shot-link").hidden = false;
    msg.textContent = `Captured ${new Date().toLocaleTimeString()} (${Math.round(blob.size / 1024)} KB). Tap to open full size.`;
  } catch (e) {
    msg.textContent = e.message;
  } finally {
    button.disabled = false;
  }
});

refresh();
setInterval(refresh, REFRESH_MS); //check every 3 secs
