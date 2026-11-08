#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_netif.h>
#include "dhcpserver/dhcpserver.h"
#include <math.h>


// Beacons and this access point stay on the same channel.
static const uint8_t RADIO_CHANNEL = 1;
static const char *AP_SSID = "CanvU-Hub";
static const char *AP_PASSWORD = "whoareyou";
static const byte DNS_PORT = 53;


// Touch module signal wire. The module drives this pin high or low.
static const int TOUCH_PIN = 7;
// 10k potentiometer wipers. Each pot's ends go to 3V3 and GND.
static const int POT_R_PIN = 4;
static const int POT_G_PIN = 5;
static const int POT_B_PIN = 6;
// 10k NTC on the supply side of a divider: 3V3 -- thermistor -- GPIO3 -- 10k -- GND.
static const int THERM_PIN = 3;
static const float SERIES_OHMS = 10000.0f;
static const float THERM_NOMINAL_OHMS = 10000.0f;
static const float THERM_NOMINAL_C = 25.0f;
static const float THERM_BETA = 3950.0f;
// Pulse sensor signal wire on an ADC1 pin. Power the sensor from 3V3.
static const int PULSE_PIN = 8;
// Sample the pulse for a short burst, then leave the analog pin alone so the radio can hear the beacons.
static const unsigned long PULSE_SAMPLE_MS = 2;
static const unsigned long PULSE_BURST_MS = 2500;
static const unsigned long PULSE_REST_MS = 4000;


volatile int rssiA = -100;
volatile int rssiB = -100;
volatile unsigned long lastSeenA = 0;
volatile unsigned long lastSeenB = 0;


int potR = 0;
int potG = 0;
int potB = 0;
int touchState = 0;
int bpm = 0;
float tempC = NAN;
unsigned long lastPrint = 0;
bool webReady = false;


WebServer server(80);
DNSServer dnsServer;


const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CanvU</title>
<style>
* { box-sizing: border-box; }
html, body { margin: 0; min-height: 100%; }
body {
  background:
    radial-gradient(900px 380px at 50% -12%, #fff8ee 0%, transparent 68%),
    #f3ecdf;
  color: #241c16;
  font-family: "Segoe UI", sans-serif;
}
.page {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: safe center;
  gap: 22px;
  padding: 28px 16px 32px;
}
header { text-align: center; }
.mark {
  margin: 0;
  font-size: 12px;
  letter-spacing: 0.28em;
  text-transform: uppercase;
  color: #8a7d70;
}
h1 {
  margin: 6px 0 0;
  font-family: Georgia, "Palatino Linotype", Palatino, serif;
  font-size: 40px;
  font-weight: 500;
  letter-spacing: -0.03em;
}
.sub {
  margin: 8px 0 0;
  color: #8a7d70;
  font-size: 14px;
}
.stage {
  width: min(520px, calc(100vw - 32px));
  display: grid;
  grid-template-columns: 78px 1fr;
  grid-template-rows: auto auto;
  gap: 8px 10px;
  align-items: stretch;
}
.y-labels, .x-labels {
  display: flex;
  justify-content: space-between;
  color: #8a7d70;
  font-size: 12px;
  letter-spacing: 0.01em;
}
.y-labels {
  flex-direction: column;
  align-items: flex-end;
  text-align: right;
  padding: 6px 0;
}
.x-labels { grid-column: 2; padding: 0 6px; }
.name {
  color: #241c16;
  font-family: Georgia, "Palatino Linotype", Palatino, serif;
  font-size: 16px;
}
canvas {
  width: 100%;
  aspect-ratio: 1;
  display: block;
  background: #fffaf4;
  border: 1px solid #e4d9c8;
  border-radius: 22px;
  box-shadow: 0 18px 44px rgba(70, 48, 22, 0.08);
}
.cards {
  width: min(520px, calc(100vw - 32px));
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.card {
  background: rgba(255, 250, 244, 0.82);
  border: 1px solid #e4d9c8;
  border-radius: 16px;
  padding: 12px 14px;
}
.card .k {
  color: #8a7d70;
  font-size: 12px;
  letter-spacing: 0.04em;
}
.card .v {
  margin-top: 4px;
  font-size: 22px;
  font-variant-numeric: tabular-nums;
}
#coords {
  font-size: 18px;
  line-height: 1.3;
  white-space: pre-line;
}
.card .live {
  margin-top: 2px;
  color: #8a7d70;
  font-size: 12px;
}
.wide { grid-column: 1 / -1; }
.color-row {
  display: flex;
  align-items: center;
  gap: 12px;
}
.swatch {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  border: 2px solid rgba(255, 255, 255, 0.9);
  box-shadow: 0 0 0 1px #e4d9c8;
  background: rgb(0, 0, 0);
  flex: none;
}
.hint { margin-top: 6px; color: #8a7d70; font-size: 12px; line-height: 1.4; }
.points {
  margin-top: 6px;
  font-size: 13px;
  line-height: 1.55;
  white-space: pre-line;
}
button {
  border: 1px solid #e4d9c8;
  background: #fffaf4;
  color: #241c16;
  border-radius: 999px;
  padding: 8px 16px;
  font: inherit;
  font-size: 14px;
}
.actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-top: 12px;
}
</style>
</head>
<body>
<div class="page">
  <header>
    <p class="mark">CanvU</p>
    <h1>Position</h1>
    <p class="sub">Corners and edges, three times, counterclockwise from the bottom left. On a beacon the strongest reading is kept, and the farthest point keeps the weakest.</p>
    <p class="sub" id="prompt">Pass 1 of 3: hold touch at the bottom left, then let go</p>
    <p class="sub" id="tempNow">Temperature --</p>
    <div class="actions">
      <button id="recal" type="button">Calibrate again</button>
      <button id="clear" type="button">Clear drawing</button>
    </div>
  </header>


  <div class="stage">
    <div class="y-labels">
      <span>Far</span>
      <span class="name">Y</span>
      <span>Near</span>
    </div>
    <canvas id="canvas"></canvas>
    <div class="x-labels">
      <span>B</span>
      <span class="name">X</span>
      <span>A</span>
    </div>
  </div>


  <section class="cards">
    <article class="card">
      <div class="k">Beacon A</div>
      <div class="v" id="aValue">--</div>
      <div class="live" id="aLive">live --</div>
    </article>
    <article class="card">
      <div class="k">Beacon B</div>
      <div class="v" id="bValue">--</div>
      <div class="live" id="bLive">live --</div>
    </article>
    <article class="card">
      <div class="k">On the plane</div>
      <div class="v" id="coords">--</div>
    </article>
    <article class="card">
      <div class="k">Touch</div>
      <div class="v" id="touch">Off</div>
    </article>
    <article class="card wide">
      <div class="k">Calibration points</div>
      <div class="points" id="points">Bottom left: --</div>
    </article>
    <article class="card">
      <div class="k">Temperature</div>
      <div class="v" id="temp">--</div>
    </article>
    <article class="card">
      <div class="k">Heart rate</div>
      <div class="v" id="bpm">--</div>
    </article>
    <article class="card wide">
      <div class="k">Knobs</div>
      <div class="v" id="pots">R 0 | G 0 | B 0</div>
    </article>
    <article class="card wide">
      <div class="color-row">
        <div class="swatch" id="swatch"></div>
        <div>
          <div class="k">Dot color</div>
          <div class="v" id="colorText">rgb(0, 0, 0)</div>
        </div>
      </div>
      <div class="hint">Knobs set the base color. A high heart rate pulls the dot toward red and lowers green and blue a little. Temperature shifts green.</div>
    </article>
  </section>
</div>
<script>
const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const ink = document.createElement("canvas");
const inkCtx = ink.getContext("2d");


const RSSI_SMOOTHING = 0.25;
const PLANE = 1;
// 6 dB doubles the distance. Calibration readings are distance 1 across the box.
const PATH_N = 2;
// The dot can move this far across the box in one frame.
const MAX_STEP = 0.08;


let rssiA = -100;
let rssiB = -100;
let smoothA = -100;
let smoothB = -100;
let bpm = 0;
let tempC = null;
let potR = 0;
let potG = 0;
let potB = 0;
let touching = false;
let lastInkX = null;
let lastInkY = null;
let drawX = null;
let drawY = null;
let shownR = 0;
let shownG = 0;
let shownB = 0;
let calibrating = true;
let calPass = 1;
let corners = [];
let passes = [];
let holdA = [];
let holdB = [];
let touchWasOn = false;


const CAL_STEPS = [
  "Hold touch at the bottom left, then let go",
  "Hold touch at the bottom middle, then let go",
  "Hold touch at the bottom right, then let go",
  "Hold touch at the middle right, then let go",
  "Hold touch at the top right, then let go",
  "Hold touch at the top middle, then let go",
  "Hold touch at the top left, then let go",
  "Hold touch at the middle left, then let go"
];
const POINT_NAMES = [
  "Bottom left", "Bottom middle", "Bottom right", "Middle right",
  "Top right", "Top middle", "Top left", "Middle left"
];
const GRID = [
  { u: 0, v: 0 },
  { u: 0.5, v: 0 },
  { u: 1, v: 0 },
  { u: 1, v: 0.5 },
  { u: 1, v: 1 },
  { u: 0.5, v: 1 },
  { u: 0, v: 1 },
  { u: 0, v: 0.5 }
];


function fit() {
  const dpr = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (width === 0 || height === 0) return;
  const nextWidth = Math.round(width * dpr);
  const nextHeight = Math.round(height * dpr);
  if (canvas.width !== nextWidth || canvas.height !== nextHeight) {
    const previous = document.createElement("canvas");
    previous.width = ink.width;
    previous.height = ink.height;
    if (previous.width > 0 && previous.height > 0) {
      previous.getContext("2d").drawImage(ink, 0, 0);
    }
    canvas.width = nextWidth;
    canvas.height = nextHeight;
    ink.width = nextWidth;
    ink.height = nextHeight;
    if (previous.width > 0 && previous.height > 0) {
      inkCtx.setTransform(1, 0, 0, 1, 0, 0);
      inkCtx.drawImage(previous, 0, 0, nextWidth, nextHeight);
    }
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  inkCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
}


function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}


function readingToDistance(rssi, rssiAtUnit) {
  const distance = Math.pow(10, (rssiAtUnit - rssi) / (10 * PATH_N));
  if (distance < 0.05) return 0.05;
  if (distance > 8) return 8;
  return distance;
}


function distancePoint(readingA, readingB) {
  return {
    x: readingToDistance(readingB, corners[1].b),
    y: readingToDistance(readingA, corners[0].a)
  };
}


function cross(p, q) {
  return p.x * q.y - p.y * q.x;
}


function solveBilinear(p, bl, br, tl, tr) {
  const e = { x: br.x - bl.x, y: br.y - bl.y };
  const f = { x: tl.x - bl.x, y: tl.y - bl.y };
  const g = { x: bl.x - br.x - tl.x + tr.x, y: bl.y - br.y - tl.y + tr.y };
  const h = { x: p.x - bl.x, y: p.y - bl.y };
  const k2 = cross(g, f);
  const k1 = cross(e, f) + cross(h, g);
  const k0 = cross(h, e);
  const vs = [];
  if (Math.abs(k2) < 1e-4) {
    if (Math.abs(k1) > 1e-4) vs.push(-k0 / k1);
  } else {
    const disc = k1 * k1 - 4 * k2 * k0;
    if (disc >= 0) {
      const root = Math.sqrt(disc);
      vs.push((-k1 - root) / (2 * k2));
      vs.push((-k1 + root) / (2 * k2));
    }
  }
  let best = null;
  for (let i = 0; i < vs.length; i++) {
    const v = vs[i];
    const denom = { x: e.x + g.x * v, y: e.y + g.y * v };
    let u = null;
    if (Math.abs(denom.x) >= Math.abs(denom.y) && Math.abs(denom.x) > 1e-4) u = (h.x - f.x * v) / denom.x;
    else if (Math.abs(denom.y) > 1e-4) u = (h.y - f.y * v) / denom.y;
    if (u === null || !Number.isFinite(u) || !Number.isFinite(v)) continue;
    const du = u < 0 ? -u : (u > 1 ? u - 1 : 0);
    const dv = v < 0 ? -v : (v > 1 ? v - 1 : 0);
    const score = du * du + dv * dv;
    if (best === null || score < best.score) best = { u: u, v: v, score: score };
  }
  return best;
}


function placeOnPlane(readingA, readingB) {
  const hit = solveBilinear(
    distancePoint(readingA, readingB),
    distancePoint(corners[0].a, corners[0].b),
    distancePoint(corners[1].a, corners[1].b),
    distancePoint(corners[3].a, corners[3].b),
    distancePoint(corners[2].a, corners[2].b)
  );
  if (!hit) return { x: 0.5, y: 0.5 };
  return { x: clamp01(hit.u), y: clamp01(hit.v) };
}


function potToColor(raw) {
  const value = Number(raw);
  if (!Number.isFinite(value)) return 0;
  return Math.round(clamp01(value / 4095) * 255);
}


function clamp255(value) {
  return Math.round(Math.max(0, Math.min(255, value)));
}


function formatDbm(value) {
  return (Math.round(value * 10) / 10).toFixed(1);
}


function spotFromCalibration() {
  const CORNER_REACH = 1.35;
  let weightSum = 0;
  let uSum = 0;
  let vSum = 0;
  for (let i = 0; i < GRID.length; i++) {
    const da = smoothA - corners[i].a;
    const db = smoothB - corners[i].b;
    const nearness = da * da + db * db + 1;
    const weight = 1 / (nearness * nearness);
    const corner = GRID[i].u !== 0.5 && GRID[i].v !== 0.5;
    const reach = corner ? CORNER_REACH : 1;
    const u = 0.5 + (GRID[i].u - 0.5) * reach;
    const v = 0.5 + (GRID[i].v - 0.5) * reach;
    weightSum += weight;
    uSum += weight * u;
    vSum += weight * v;
  }
  return {
    x: clamp01(uSum / weightSum),
    y: clamp01(vSum / weightSum)
  };
}


function dist2FromA(i) {
  const du = GRID[i].u - 1;
  const dv = GRID[i].v;
  return du * du + dv * dv;
}


function dist2FromB(i) {
  return GRID[i].u * GRID[i].u + GRID[i].v * GRID[i].v;
}


function least(xs) {
  let value = xs[0];
  for (let i = 1; i < xs.length; i++) if (xs[i] < value) value = xs[i];
  return value;
}


function greatest(xs) {
  let value = xs[0];
  for (let i = 1; i < xs.length; i++) if (xs[i] > value) value = xs[i];
  return value;
}


function enforceDecrease(values, dist2) {
  const order = [];
  for (let i = 0; i < values.length; i++) order.push(i);
  order.sort(function (i, j) { return dist2[i] - dist2[j]; });
  const out = values.slice();
  for (let n = 0; n < order.length; n++) {
    const i = order[n];
    let cap = null;
    for (let m = 0; m < n; m++) {
      const j = order[m];
      if (dist2[j] < dist2[i] - 1e-6) {
        const limit = out[j] - 1;
        if (cap === null || limit < cap) cap = limit;
      }
    }
    if (cap !== null && out[i] > cap) out[i] = cap;
  }
  return out;
}


function applyDecrease() {
  const rawA = [];
  const rawB = [];
  const distA = [];
  const distB = [];
  for (let i = 0; i < corners.length; i++) {
    rawA.push(corners[i].a);
    rawB.push(corners[i].b);
    distA.push(dist2FromA(i));
    distB.push(dist2FromB(i));
  }
  const fixedA = enforceDecrease(rawA, distA);
  const fixedB = enforceDecrease(rawB, distB);
  for (let i = 0; i < corners.length; i++) {
    corners[i] = { a: fixedA[i], b: fixedB[i] };
  }
}


function extremeValues(key, distAt) {
  const n = GRID.length;
  const dist = [];
  let nearD = distAt(0);
  let farD = nearD;
  dist.push(nearD);
  for (let i = 1; i < n; i++) {
    dist.push(distAt(i));
    if (dist[i] < nearD) nearD = dist[i];
    if (dist[i] > farD) farD = dist[i];
  }
  const out = [];
  const ready = [];
  for (let i = 0; i < n; i++) {
    const xs = [passes[0][i][key], passes[1][i][key], passes[2][i][key]];
    if (Math.abs(dist[i] - nearD) < 1e-6) {
      out.push(greatest(xs));
      ready.push(true);
    } else if (Math.abs(dist[i] - farD) < 1e-6) {
      out.push(least(xs));
      ready.push(true);
    } else {
      out.push(0);
      ready.push(false);
    }
  }
  const order = [];
  for (let i = 0; i < n; i++) order.push(i);
  order.sort(function (i, j) { return dist[i] - dist[j]; });
  for (let step = 0; step < order.length; step++) {
    const i = order[step];
    if (ready[i]) continue;
    const xs = [passes[0][i][key], passes[1][i][key], passes[2][i][key]];
    const hi = greatest(xs);
    const lo = least(xs);
    let ceiling = null;
    for (let j = 0; j < n; j++) {
      if (ready[j] && dist[j] < dist[i] - 1e-6) {
        const limit = out[j] - 1;
        if (ceiling === null || limit < ceiling) ceiling = limit;
      }
    }
    out[i] = ceiling !== null && hi <= ceiling ? hi : lo;
    ready[i] = true;
  }
  return out;
}


function chooseExtremes() {
  const chosenA = extremeValues("a", dist2FromA);
  const chosenB = extremeValues("b", dist2FromB);
  corners = [];
  for (let i = 0; i < chosenA.length; i++) {
    corners.push({ a: chosenA[i], b: chosenB[i] });
  }
  applyDecrease();
}


function signalPair(point) {
  if (!point) return "--";
  return "A " + formatDbm(point.a) + " dBm, B " + formatDbm(point.b) + " dBm";
}


function showCornerSignals() {
  const lines = [];
  for (let i = 0; i < POINT_NAMES.length; i++) {
    lines.push(POINT_NAMES[i] + ": " + signalPair(corners[i]));
  }
  document.getElementById("points").textContent = lines.join("\n");
}


function cornerLabel(i) {
  const point = corners[i];
  if (!point) return "";
  return "A " + Math.round(point.a) + "  B " + Math.round(point.b);
}


function updatePrompt() {
  const el = document.getElementById("prompt");
  if (smoothA <= -100 || smoothB <= -100) {
    el.textContent = "Waiting for both beacons";
    return;
  }
  el.textContent = calibrating
    ? "Pass " + calPass + " of 3: " + CAL_STEPS[corners.length]
    : "Touch on leaves a mark that stays.";
}


function rememberCorners() {
  try {
    localStorage.setItem("canvu-extremes", JSON.stringify(corners));
  } catch (error) {}
}


function loadCorners() {
  try {
    const saved = JSON.parse(localStorage.getItem("canvu-extremes") || "null");
    if (saved && saved.length === GRID.length) {
      corners = saved;
      applyDecrease();
      calibrating = false;
    }
  } catch (error) {}
}


function resetCalibration() {
  calibrating = true;
  calPass = 1;
  corners = [];
  passes = [];
  holdA = [];
  holdB = [];
  lastInkX = null;
  lastInkY = null;
  drawX = null;
  drawY = null;
  try {
    localStorage.removeItem("canvu-extremes");
    localStorage.removeItem("canvu-corners3");
    localStorage.removeItem("canvu-corners");
    localStorage.removeItem("canvu-grid");
  } catch (error) {}
  inkCtx.save();
  inkCtx.setTransform(1, 0, 0, 1, 0, 0);
  inkCtx.clearRect(0, 0, ink.width, ink.height);
  inkCtx.restore();
  updatePrompt();
}


function applyReading(data) {
  rssiA = Number(data.rssiA);
  rssiB = Number(data.rssiB);
  bpm = Number(data.bpm) || 0;
  tempC = data.temp === null || data.temp === undefined ? null : Number(data.temp);
  potR = potToColor(data.r);
  potG = potToColor(data.g);
  potB = potToColor(data.b);


  const nextTouch = data.touch ? true : false;
  if (!nextTouch) {
    lastInkX = null;
    lastInkY = null;
  }
  touching = nextTouch;


  document.getElementById("aLive").textContent = "live " + rssiA + " dBm";
  document.getElementById("bLive").textContent = "live " + rssiB + " dBm";
  document.getElementById("touch").textContent = data.touch ? "On" : "Off";
  document.getElementById("pots").textContent = "R " + data.r + " | G " + data.g + " | B " + data.b;
  document.getElementById("temp").textContent = tempC === null || Number.isNaN(tempC) ? "--" : tempC.toFixed(1) + " C";
  document.getElementById("tempNow").textContent = tempC === null || Number.isNaN(tempC) ? "Temperature --" : "Temperature " + tempC.toFixed(1) + " C";
  document.getElementById("bpm").textContent = bpm + " bpm";
}


async function updateRSSI() {
  try {
    const response = await fetch("/rssi", { cache: "no-store" });
    applyReading(await response.json());
  } catch (error) {}
}


function draw() {
  fit();
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;


  if (rssiA > -100) {
    if (smoothA <= -100) smoothA = rssiA;
    else smoothA += (rssiA - smoothA) * RSSI_SMOOTHING;
  }
  if (rssiB > -100) {
    if (smoothB <= -100) smoothB = rssiB;
    else smoothB += (rssiB - smoothB) * RSSI_SMOOTHING;
  }


  let spot = { x: PLANE / 2, y: PLANE / 2 };
  if (!calibrating && corners.length === GRID.length && smoothA > -100 && smoothB > -100) {
    const hit = spotFromCalibration();
    if (drawX === null) {
      drawX = hit.x;
      drawY = hit.y;
    } else {
      const dx = hit.x - drawX;
      const dy = hit.y - drawY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      if (dist > MAX_STEP) {
        drawX += dx / dist * MAX_STEP;
        drawY += dy / dist * MAX_STEP;
      } else {
        drawX = hit.x;
        drawY = hit.y;
      }
    }
    spot = { x: drawX, y: drawY };
  } else if (calibrating) {
    drawX = null;
    drawY = null;
    const next = GRID[Math.min(corners.length, GRID.length - 1)];
    spot = { x: next.u, y: next.v };
  }


  if (calibrating && touching && smoothA > -100 && smoothB > -100) {
    holdA.push(smoothA);
    holdB.push(smoothB);
    if (holdA.length > 20) {
      holdA.shift();
      holdB.shift();
    }
  }
  if (touchWasOn && !touching && calibrating && holdA.length >= 8) {
    let sumA = 0;
    let sumB = 0;
    for (let i = 0; i < holdA.length; i++) {
      sumA += holdA[i];
      sumB += holdB[i];
    }
    corners.push({ a: sumA / holdA.length, b: sumB / holdB.length });
    if (corners.length === GRID.length) {
      passes[calPass - 1] = corners.slice();
      if (calPass < 3) {
        corners = [];
        calPass += 1;
      } else {
        chooseExtremes();
        calibrating = false;
        rememberCorners();
      }
    }
  }
  if (!touching) {
    holdA = [];
    holdB = [];
  }
  touchWasOn = touching;
  updatePrompt();
  const pad = 20;
  const x = pad + (spot.x / PLANE) * (width - pad * 2);
  const y = pad + (1 - spot.y / PLANE) * (height - pad * 2);


  const effort = bpm > 60 ? clamp01((bpm - 60) / 80) : 0;
  const red = clamp255(potR + effort * (255 - potR));
  const tempShift = tempC === null || Number.isNaN(tempC) ? 0 : (tempC - 20) * 18;
  const green = clamp255(potG + tempShift - effort * 40);
  const blue = clamp255(potB - effort * 40);
  shownR += (red - shownR) * 0.008;
  shownG += (green - shownG) * 0.008;
  shownB += (blue - shownB) * 0.008;
  const color = "rgb(" + Math.round(shownR) + ", " + Math.round(shownG) + ", " + Math.round(shownB) + ")";


  if (touching && !calibrating && width > 0 && height > 0) {
    inkCtx.strokeStyle = color;
    inkCtx.fillStyle = color;
    inkCtx.lineWidth = 8;
    inkCtx.lineCap = "round";
    inkCtx.lineJoin = "round";
    const planePx = (width - pad * 2) / PLANE;
    if (lastInkX === null || Math.sqrt((x - lastInkX) * (x - lastInkX) + (y - lastInkY) * (y - lastInkY)) > planePx * 0.15) {
      inkCtx.beginPath();
      inkCtx.arc(x, y, 4, 0, Math.PI * 2);
      inkCtx.fill();
    } else {
      inkCtx.beginPath();
      inkCtx.moveTo(lastInkX, lastInkY);
      inkCtx.lineTo(x, y);
      inkCtx.stroke();
    }
    lastInkX = x;
    lastInkY = y;
  }


  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = "rgba(80, 58, 36, 0.1)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 1; i < 4; i++) {
    const at = (i / 4) * width;
    ctx.moveTo(at, 0);
    ctx.lineTo(at, height);
    ctx.moveTo(0, at);
    ctx.lineTo(width, at);
  }
  ctx.stroke();
  ctx.drawImage(ink, 0, 0, width, height);


  const guide = GRID;
  for (let i = 0; i < guide.length; i++) {
    const gx = pad + guide[i].u * (width - pad * 2);
    const gy = pad + (1 - guide[i].v) * (height - pad * 2);
    ctx.beginPath();
    ctx.arc(gx, gy, 5, 0, Math.PI * 2);
    if (!calibrating || i < corners.length) {
      ctx.fillStyle = "rgba(36, 28, 22, 0.45)";
      ctx.fill();
    } else if (i === corners.length) {
      ctx.lineWidth = 2;
      ctx.strokeStyle = "rgba(36, 28, 22, 0.7)";
      ctx.stroke();
    }
    const label = cornerLabel(i);
    if (label) {
      ctx.font = "11px Segoe UI, sans-serif";
      ctx.fillStyle = "#241c16";
      const align = guide[i].u > 0.6 ? "right" : (guide[i].u < 0.4 ? "left" : "center");
      const baseline = guide[i].v > 0.6 ? "bottom" : "top";
      ctx.textAlign = align;
      ctx.textBaseline = baseline;
      const lx = gx + (align === "right" ? -8 : (align === "left" ? 8 : 0));
      const ly = gy + (baseline === "bottom" ? -8 : 8);
      ctx.fillText(label, lx, ly);
    }
  }


  ctx.strokeStyle = "rgba(80, 58, 36, 0.16)";
  ctx.beginPath();
  ctx.moveTo(x, 0);
  ctx.lineTo(x, height);
  ctx.moveTo(0, y);
  ctx.lineTo(width, y);
  ctx.stroke();


  ctx.beginPath();
  ctx.arc(x, y, 12, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.shadowColor = color;
  ctx.shadowBlur = 10;
  ctx.fill();
  ctx.shadowBlur = 0;
  ctx.lineWidth = 2;
  ctx.strokeStyle = "rgba(255, 250, 244, 0.92)";
  ctx.stroke();


  document.getElementById("aValue").textContent = formatDbm(smoothA) + " dBm";
  document.getElementById("bValue").textContent = formatDbm(smoothB) + " dBm";
  document.getElementById("coords").textContent = calibrating
    ? "Pass " + calPass + ", " + corners.length + " of " + GRID.length
    : "X " + spot.x.toFixed(2) + "\nY " + spot.y.toFixed(2);
  document.getElementById("colorText").textContent = color;
  document.getElementById("swatch").style.background = color;
  showCornerSignals();


  requestAnimationFrame(draw);
}


function clearDrawing() {
  lastInkX = null;
  lastInkY = null;
  inkCtx.save();
  inkCtx.setTransform(1, 0, 0, 1, 0, 0);
  inkCtx.clearRect(0, 0, ink.width, ink.height);
  inkCtx.restore();
}


window.addEventListener("resize", fit);
document.getElementById("recal").addEventListener("click", resetCalibration);
document.getElementById("clear").addEventListener("click", clearDrawing);
loadCorners();
updatePrompt();
setInterval(updateRSSI, 20);
updateRSSI();
draw();
</script>
</body>
</html>
)rawliteral";


static const int RSSI_MEDIAN = 3;
static int recentA[RSSI_MEDIAN];
static int recentB[RSSI_MEDIAN];
static uint8_t countA = 0;
static uint8_t countB = 0;
static uint8_t indexA = 0;
static uint8_t indexB = 0;
static int filteredA = -1000;
static int filteredB = -1000;
static bool haveA = false;
static bool haveB = false;


int median3(int a, int b, int c) {
  if (a > b) {
    int swap = a;
    a = b;
    b = swap;
  }
  if (b > c) {
    int swap = b;
    b = c;
    c = swap;
  }
  if (a > b) {
    int swap = a;
    a = b;
    b = swap;
  }
  return b;
}


int pushRssi(int *recent, uint8_t *count, uint8_t *index, int *filtered, bool *have, int rssi) {
  recent[*index] = rssi;
  *index = (*index + 1) % RSSI_MEDIAN;
  if (*count < RSSI_MEDIAN) *count += 1;
  int sample = rssi;
  if (*count >= RSSI_MEDIAN) sample = median3(recent[0], recent[1], recent[2]);
  if (!*have) {
    *filtered = sample * 10;
    *have = true;
  } else {
    // Settle on the spot in about a second. One odd packet cannot drag it.
    *filtered = (*filtered * 7 + sample * 30) / 10;
  }
  return *filtered / 10;
}


void onSniff(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;
  if (packet->rx_ctrl.rx_state != 0) return;


  const uint8_t *frame = packet->payload;
  uint16_t length = packet->rx_ctrl.sig_len;
  if (length < 38) return;
  if (length >= 4) length -= 4;
  if ((frame[0] & 0xFC) != 0x80 && (frame[0] & 0xFC) != 0x50) return;


  // A hidden network puts an empty name first and the real name later.
  // Search the whole frame instead of stopping at that empty name.
  int rssi = packet->rx_ctrl.rssi;
  if (rssi > 0 || rssi < -100) return;
  for (uint16_t i = 0; i + 11 <= length; i++) {
    if (memcmp(frame + i, "CanvU-AxisA", 11) == 0) {
      rssiA = pushRssi(recentA, &countA, &indexA, &filteredA, &haveA, rssi);
      lastSeenA = millis();
      return;
    }
    if (memcmp(frame + i, "CanvU-AxisB", 11) == 0) {
      rssiB = pushRssi(recentB, &countB, &indexB, &filteredB, &haveB, rssi);
      lastSeenB = millis();
      return;
    }
  }
}


static const int BPM_WINDOW = 5;
static int bpmWindow[BPM_WINDOW];
static int bpmCount = 0;
static int bpmIndex = 0;


int averageBpm(int next) {
  if (next <= 0) return bpm;
  bpmWindow[bpmIndex] = next;
  bpmIndex = (bpmIndex + 1) % BPM_WINDOW;
  if (bpmCount < BPM_WINDOW) bpmCount++;
  int sum = 0;
  for (int i = 0; i < bpmCount; i++) sum += bpmWindow[i];
  return sum / bpmCount;
}


void samplePulse() {
  static unsigned long lastSampleMs = 0;
  static unsigned long phaseStartMs = 0;
  static unsigned long lastBeatMs = 0;
  static bool bursting = false;
  static bool waveHigh = false;
  static int peak = 0;
  static int trough = 4095;


  unsigned long now = millis();
  if (phaseStartMs == 0) phaseStartMs = now;


  if (!bursting) {
    if (now - phaseStartMs < PULSE_REST_MS) return;
    bursting = true;
    phaseStartMs = now;
    waveHigh = false;
  } else if (now - phaseStartMs >= PULSE_BURST_MS) {
    bursting = false;
    phaseStartMs = now;
    return;
  }


  if (now - lastSampleMs < PULSE_SAMPLE_MS) return;
  lastSampleMs = now;


  int sample = analogRead(PULSE_PIN);
  if (sample > peak) peak = sample;
  else if (peak > 0) peak--;
  if (sample < trough) trough = sample;
  else if (trough < 4095) trough++;
  if (peak < trough) {
    peak = sample;
    trough = sample;
  }


  int span = peak - trough;
  if (span < 150) {
    waveHigh = false;
    return;
  }


  int highLine = trough + (span * 3) / 4;
  int lowLine = trough + span / 2;


  if (!waveHigh && sample > highLine) {
    waveHigh = true;
    if (lastBeatMs != 0) {
      unsigned long interval = now - lastBeatMs;
      if (interval >= 300 && interval <= 2000) bpm = averageBpm((int)(60000UL / interval));
    }
    lastBeatMs = now;
  } else if (waveHigh && sample < lowLine) {
    waveHigh = false;
  }


}


float readTemperatureC() {
  int adc = 0;
  for (int i = 0; i < 8; i++) adc += analogRead(THERM_PIN);
  adc /= 8;
  if (adc <= 0 || adc >= 4095) return NAN;


  float resistance = SERIES_OHMS * (float)(4095 - adc) / (float)adc;
  float kelvin = 1.0f / (1.0f / (THERM_NOMINAL_C + 273.15f) + log(resistance / THERM_NOMINAL_OHMS) / THERM_BETA);
  return kelvin - 273.15f;
}


void updateSensors() {
  potR = analogRead(POT_R_PIN);
  potG = analogRead(POT_G_PIN);
  potB = analogRead(POT_B_PIN);
  touchState = digitalRead(TOUCH_PIN) == HIGH ? 1 : 0;
  tempC = readTemperatureC();
}


void formatTemp(char *out, size_t length) {
  if (isnan(tempC)) {
    snprintf(out, length, "null");
    return;
  }


  int tenths = (int)roundf(tempC * 10.0f);
  const char *sign = "";
  if (tenths < 0) {
    sign = "-";
    tenths = -tenths;
  }
  snprintf(out, length, "%s%d.%d", sign, tenths / 10, tenths % 10);
}


void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", webpage);
}


void redirectToPortal() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}


void handleRSSI() {
  char tempText[16];
  char json[192];
  formatTemp(tempText, sizeof(tempText));
  snprintf(json, sizeof(json),
           "{\"rssiA\":%d,\"rssiB\":%d,\"touch\":%d,\"r\":%d,\"g\":%d,\"b\":%d,\"temp\":%s,\"bpm\":%d}",
           (int)rssiA, (int)rssiB, touchState, potR, potG, potB, tempText, bpm);


  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}


void handleNotFound() {
  String host = server.hostHeader();
  if (host == "192.168.4.1" || host.length() == 0) {
    handleRoot();
    return;
  }
  redirectToPortal();
}


void offerCaptiveDns() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) return;


  esp_netif_dhcps_stop(ap);


  dhcps_offer_t offer = OFFER_DNS;
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));


  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = (uint32_t)WiFi.softAPIP();
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);


  esp_netif_dhcps_start(ap);
}


void startWebServer() {
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(0);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());


  server.on("/", HTTP_GET, handleRoot);
  server.on("/rssi", HTTP_GET, handleRSSI);
  server.on("/generate_204", redirectToPortal);
  server.on("/gen_204", redirectToPortal);
  server.on("/hotspot-detect.html", redirectToPortal);
  server.on("/library/test/success.html", redirectToPortal);
  server.on("/connecttest.txt", redirectToPortal);
  server.on("/ncsi.txt", redirectToPortal);
  server.on("/canonical.html", redirectToPortal);
  server.on("/success.txt", redirectToPortal);
  server.onNotFound(handleNotFound);
  server.begin();
  webReady = true;
}


void setup() {
  pinMode(TOUCH_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  updateSensors();


  Serial.begin(115200);
  delay(300);


  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);


  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, RADIO_CHANNEL, 0, 4)) {
    Serial.println("AP failed");
  } else {
    esp_wifi_set_ps(WIFI_PS_NONE);
    offerCaptiveDns();
  }


  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&onSniff);
  esp_wifi_set_promiscuous(true);


  if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) startWebServer();


  Serial.println("CANVU HUB READY");
  Serial.print("Network: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Website: http://");
  Serial.println(WiFi.softAPIP());
}


void loop() {
  if (webReady) {
    for (int i = 0; i < 4; i++) dnsServer.processNextRequest();
    server.handleClient();
  }
  samplePulse();


  unsigned long now = millis();
  if (lastSeenA != 0 && now - lastSeenA > 2000) rssiA = -100;
  if (lastSeenB != 0 && now - lastSeenB > 2000) rssiB = -100;
  if (now - lastPrint < 500) return;
  lastPrint = now;


  updateSensors();


  Serial.print("signal A: ");
  Serial.print((int)rssiA);
  Serial.print("  signal B: ");
  Serial.print((int)rssiB);
  Serial.print("  touch: ");
  Serial.print(touchState ? "on" : "off");
  Serial.print("  R: ");
  Serial.print(potR);
  Serial.print("  G: ");
  Serial.print(potG);
  Serial.print("  B: ");
  Serial.print(potB);
  Serial.print("  temp: ");
  if (isnan(tempC)) Serial.print("--");
  else {
    Serial.print(tempC, 1);
    Serial.print(" C");
  }
  Serial.print("  bpm: ");
  Serial.println(bpm);
}





