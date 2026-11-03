import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import createPatternCore from './wasm/pattern_core.js';

// ---------------------------------------------------------------------------
// Shared integer core (WASM)
// ---------------------------------------------------------------------------

const TIER_NAMES = ['ordinary', 'nominal', 'optional'];
const OP_NAMES = [
  'keyframes', 'sine', 'travelling_wave', 'interference',
  'spiral', 'ripple', 'cascade', 'random_walk',
];

let core = null;
let pc = null;

async function initCore() {
  core = await createPatternCore();
  pc = {
    load: core.cwrap('pc_load_json', 'number', ['string', 'number']),
    free: core.cwrap('pc_free', null, ['number']),
    lastError: core.cwrap('pc_last_error', 'string', []),
    gridCols: core.cwrap('pc_grid_cols', 'number', ['number']),
    gridRows: core.cwrap('pc_grid_rows', 'number', ['number']),
    dtMs: core.cwrap('pc_dt_ms', 'number', ['number']),
    n: core.cwrap('pc_n', 'number', ['number']),
    durationMs: core.cwrap('pc_duration_ms', 'number', ['number']),
    tier: core.cwrap('pc_tier', 'number', ['number']),
    layerCount: core.cwrap('pc_layer_count', 'number', ['number']),
    layerOp: core.cwrap('pc_layer_op', 'number', ['number', 'number']),
    layerAxis: core.cwrap('pc_layer_axis', 'number', ['number', 'number']),
    evalUm: core.cwrap('pc_eval_um', 'number', ['number', 'number', 'number']),
    report: core.cwrap('pc_report_json', 'string', ['number']),
    busLoad: core.cwrap('pc_bus_load_percent', 'number', ['number']),
    umToCode: core.cwrap('mc_um_to_code', 'number', ['number']),
    codeToUm: core.cwrap('mc_code_to_um', 'number', ['number']),
    umToSteps: core.cwrap('mc_um_to_steps', 'number', ['number']),
    stepsToUm: core.cwrap('mc_steps_to_um', 'number', ['number']),
  };
}

// Serialize a built-in show into the version-1 .anim.json fields the core
// understands.  Loaded files go straight to the core as their original text.
function showJson(show, duration_ms) {
  const layers = show.layers.map((l) => ({
    op: l.op || 'sine',
    amplitude_um: l.amplitude_um || 0,
    period_ms: l.period_ms || 0,
    wavelength_cols: l.wavelength_cols || 0,
    phase_deg: l.phase_deg || 0,
    axis: l.axis || 'col',
    direction: l.direction ?? 1,
    seed: l.seed || 0,
    amplitude2_um: l.amplitude2_um || 0,
    period2_ms: l.period2_ms || 0,
    wavelength2_cols: l.wavelength2_cols || 0,
    phase2_deg: l.phase2_deg || 0,
    axis2: l.axis2 || 'col',
  }));
  return JSON.stringify({
    version: 1,
    grid: [show.grid_cols, show.grid_rows],
    dt_ms: show.dt_ms,
    n: show.n,
    duration_ms,
    tier: show.tier || 'ordinary',
    base_um: 1000000,
    layers,
  });
}

function reportFromHandle(handle) {
  let raw = { violations: [] };
  try { raw = JSON.parse(pc.report(handle)); } catch { /* keep empty */ }
  const violations = (raw.violations || []).map((v) => ({
    ball_index: v.ball,
    time_ms: v.time_ms,
    limit_name: v.limit,
    measured_um: v.measured,
    allowed_um: v.allowed,
  }));
  return {
    count: violations.length,
    violations,
    parse_error: raw.parse_error || '',
  };
}

function showFromHandle(name, handle) {
  const nLayers = pc.layerCount(handle);
  const layers = [];
  for (let i = 0; i < nLayers; i++) {
    layers.push({
      op: OP_NAMES[pc.layerOp(handle, i)] || 'sine',
      axis: pc.layerAxis(handle, i) === 1 ? 'row' : 'col',
    });
  }
  const cols = pc.gridCols(handle);
  const rows = pc.gridRows(handle);
  return {
    name,
    coreHandle: handle,
    version: 1,
    grid: [cols, rows],
    grid_cols: cols,
    grid_rows: rows,
    dt_ms: pc.dtMs(handle),
    n: pc.n(handle),
    duration_ms: pc.durationMs(handle),
    tier: TIER_NAMES[pc.tier(handle)] || 'ordinary',
    layers,
    n_layers: nLayers,
    report: reportFromHandle(handle),
    report_json: pc.report(handle),
  };
}

function ensureCoreShow(show) {
  if (show.coreHandle) return show.coreHandle;
  const duration = Math.max(show.duration_ms || computeLoopPeriodMs(show),
                            show.dt_ms || 10);
  const handle = pc.load(showJson(show, duration), 0);
  if (!handle) throw new Error(pc.lastError());
  show.coreHandle = handle;
  show.duration_ms = duration;
  return handle;
}

// Render-time evaluation uses the same integer path as the bake.
function evaluatePositionUm(show, col, row, time_ms) {
  if (!show.coreHandle) return 1000000;
  const ball = row * show.grid_cols + col;
  let um = pc.evalUm(show.coreHandle, ball, Math.round(time_ms));
  if (um < 0) um = 1000000;
  if (um < 0) um = 0;
  else if (um > 2000000) um = 2000000;
  return um;
}

// ---------------------------------------------------------------------------
// Show definitions
// ---------------------------------------------------------------------------

function gcd(a, b) {
  while (b) { const t = a % b; a = b; b = t; }
  return a;
}
function lcm(a, b) { return (a / gcd(a, b)) * b; }

// Natural loop period is the LCM of the layer periods, so the wrapped state
// equals the start state and looping never jumps.
function computeLoopPeriodMs(show) {
  let period = 1;
  let hasPeriod = false;
  for (let i = 0; i < show.n_layers; i++) {
    const p = Math.round(show.layers[i].period_ms);
    if (Number.isFinite(p) && p > 0) {
      period = lcm(period, p);
      hasPeriod = true;
    }
  }
  if (!hasPeriod) period = show.duration_ms || 1;
  if (show.dt_ms && period % show.dt_ms !== 0) {
    period = Math.ceil(period / show.dt_ms) * show.dt_ms;
  }
  return Math.max(period, show.dt_ms || 1);
}

function makeShow(name, layerDefs, extra = {}) {
  const layers = layerDefs.map((l) => ({
    op: l.op || 'sine',
    amplitude_um: l.amplitude_um,
    period_ms: l.period_ms,
    wavelength_cols: l.wavelength_cols,
    axis: l.axis,
    phase_deg: Math.round(((l.phase_rad || 0) * 180) / Math.PI),
  }));
  return {
    name,
    version: 1,
    grid: [10, 12],
    grid_cols: 10,
    grid_rows: 12,
    dt_ms: 10,
    n: 3,
    duration_ms: 0,
    tier: 'ordinary',
    layers,
    n_layers: layers.length,
    ...extra,
  };
}

const shows = [
  makeShow('Golden Sine', [
    { axis: 'col', amplitude_um: 50000, period_ms: 5000, wavelength_cols: 10 },
  ]),
  makeShow('X / Y Waves (showcase)', [
    { axis: 'col', amplitude_um: 150000, period_ms: 8000, wavelength_cols: 10 },
    { axis: 'row', amplitude_um: 150000, period_ms: 6000, wavelength_cols: 12 },
  ]),
  makeShow('Interference', [
    { axis: 'col', amplitude_um: 100000, period_ms: 7000, wavelength_cols: 10 },
    { axis: 'col', amplitude_um: 100000, period_ms: 11000, wavelength_cols: 6, phase_rad: 1.2 },
  ]),
  makeShow('Diagonal Sweep', [
    { axis: 'col', amplitude_um: 120000, period_ms: 9000, wavelength_cols: 10 },
    { axis: 'row', amplitude_um: 120000, period_ms: 9000, wavelength_cols: 12 },
  ]),
  makeShow('Slow Tides (long)', [
    { axis: 'col', amplitude_um: 120000, period_ms: 12000, wavelength_cols: 8 },
    { axis: 'row', amplitude_um: 100000, period_ms: 15000, wavelength_cols: 12 },
  ]),
];

// ---------------------------------------------------------------------------
// Three.js scene
// ---------------------------------------------------------------------------

const container = document.getElementById('canvas-container');
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x080810);

const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 100);
camera.position.set(3.4, 2.2, 4.4);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
container.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.05;
controls.target.set(0, 1.0, 0);
controls.maxPolarAngle = Math.PI * 0.9;
controls.minPolarAngle = Math.PI * 0.1;
controls.maxDistance = 12;
controls.minDistance = 2;

const ambientLight = new THREE.AmbientLight(0x202028, 0.3);
scene.add(ambientLight);

const dirLight = new THREE.DirectionalLight(0xffffff, 1.2);
dirLight.position.set(2, 3, 2);
dirLight.castShadow = true;
scene.add(dirLight);

const pointLight = new THREE.PointLight(0x6a6a8a, 0.8);
pointLight.position.set(-2, 2, -2);
scene.add(pointLight);

const rimLight = new THREE.PointLight(0x404060, 0.4);
rimLight.position.set(0, 0, -3);
scene.add(rimLight);

// Installation volume: 3 m (X) x 2 m (Y travel) x 3 m (Z footprint).
const BOX_W = 3.0;
const BOX_H = 2.0;
const BOX_D = 3.0;

const boxGeometry = new THREE.BoxGeometry(BOX_W, BOX_H, BOX_D);
const boxEdges = new THREE.EdgesGeometry(boxGeometry);
const boxLines = new THREE.LineSegments(
  boxEdges,
  new THREE.LineBasicMaterial({ color: 0x3f6f8f, transparent: true, opacity: 0.65 })
);
boxLines.position.set(0, BOX_H / 2, 0);
scene.add(boxLines);

// Corner posts slightly brighter so the height reads clearly.
const postMat = new THREE.LineBasicMaterial({ color: 0x5c93b8, transparent: true, opacity: 0.5 });
const postGeo = new THREE.BufferGeometry();
const hx = BOX_W / 2, hz = BOX_D / 2;
const postVerts = [];
for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
  postVerts.push(sx * hx, 0, sz * hz, sx * hx, BOX_H, sz * hz);
}
postGeo.setAttribute('position', new THREE.Float32BufferAttribute(postVerts, 3));
scene.add(new THREE.LineSegments(postGeo, postMat));

const gridHelper = new THREE.GridHelper(BOX_W, 30, 0x1a1a2a, 0x0f0f1a);
gridHelper.position.y = 0.001;
scene.add(gridHelper);

const sphereGeometry = new THREE.SphereGeometry(0.04, 24, 24);

function createEnvTexture() {
  const size = 256;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  const grad = ctx.createLinearGradient(0, 0, 0, size);
  grad.addColorStop(0, '#2a2a3a');
  grad.addColorStop(0.3, '#1a1a28');
  grad.addColorStop(0.5, '#121218');
  grad.addColorStop(0.7, '#0e0e14');
  grad.addColorStop(1, '#080810');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, size, size);
  ctx.fillStyle = 'rgba(255,255,255,0.15)';
  ctx.beginPath();
  ctx.arc(size * 0.3, size * 0.2, 40, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = 'rgba(200,200,255,0.1)';
  ctx.beginPath();
  ctx.arc(size * 0.7, size * 0.4, 30, 0, Math.PI * 2);
  ctx.fill();
  const tex = new THREE.CanvasTexture(canvas);
  tex.mapping = THREE.EquirectangularReflectionMapping;
  return tex;
}

const envMap = createEnvTexture();

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let sphereMesh = null;
let sphereCount = 0;

let currentShow = null;
let currentShowIndex = -1;
let prevShow = null;
let currentCycleMs = 5000;
let currentReport = { count: 0, violations: [] };

let patternNamesKey = null;
let patternPollTimer = null;
let selectedPatternName = '';
let selectedPatternHash = '';
let patternLoading = false;
let autoReloadPatterns = true;

let simTimeMs = 0;
let lastFrameMs = null;
let isPlaying = true;
let isLooping = true;
let playSpeed = 1.0;
let currentFidelity = 'ideal';
let fidelityExaggerate = true;
const FIDELITY_ERROR_SCALE = 1000;

let blendStartWall = 0;
const BLEND_MS = 1600;

// ---------------------------------------------------------------------------
// Sphere mesh management
// ---------------------------------------------------------------------------

function ensureSpheres(count) {
  if (sphereMesh && sphereCount === count) return;

  if (sphereMesh) {
    scene.remove(sphereMesh);
    sphereMesh.material.dispose();
    sphereMesh.dispose();
  }

  const material = new THREE.MeshStandardMaterial({
    color: 0xd0d0d8,
    metalness: 0.95,
    roughness: 0.08,
    envMap: envMap,
    envMapIntensity: 1.5,
  });

  sphereMesh = new THREE.InstancedMesh(sphereGeometry, material, count);
  sphereMesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  sphereMesh.castShadow = true;
  sphereCount = count;
  scene.add(sphereMesh);
}

function updateSpherePositions(time_ms, now_ms) {
  if (!currentShow) return;

  const show = currentShow;
  const totalBalls = show.grid_cols * show.grid_rows;
  ensureSpheres(totalBalls);

  const spacingX = BOX_W / (show.grid_cols - 1);
  const spacingZ = BOX_D / (show.grid_rows - 1);

  // Blend factor between the previous show and the current one.
  let s = prevShow ? Math.min(1, (now_ms - blendStartWall) / BLEND_MS) : 1;
  const ease = s * s * (3 - 2 * s);

  const matrix = new THREE.Matrix4();
  const position = new THREE.Vector3();
  const color = new THREE.Color();

  for (let i = 0; i < totalBalls; i++) {
    const col = i % show.grid_cols;
    const row = Math.floor(i / show.grid_cols);

    let um = evaluatePositionUm(show, col, row, time_ms);
    if (prevShow && ease < 1) {
      const umOld = evaluatePositionUm(prevShow, col, row, time_ms);
      um = umOld * (1 - ease) + um * ease;
    }

    if (currentFidelity !== 'ideal') {
      const exactUm = um;
      const quantizedUm = currentFidelity === 'wire'
        ? pc.codeToUm(pc.umToCode(um))
        : pc.stepsToUm(pc.umToSteps(um));
      um = fidelityExaggerate
        ? exactUm + (quantizedUm - exactUm) * FIDELITY_ERROR_SCALE
        : quantizedUm;
    }

    const heightM = (um / 2000000) * BOX_H;
    const x = (col - (show.grid_cols - 1) / 2) * spacingX;
    const z = (row - (show.grid_rows - 1) / 2) * spacingZ;

    position.set(x, heightM, z);
    matrix.setPosition(position);
    sphereMesh.setMatrixAt(i, matrix);

    const isViolation = currentReport && currentReport.count > 0 &&
      currentReport.violations.some((v) => v.ball_index === i &&
        Math.abs(v.time_ms - time_ms) < show.dt_ms);

    color.setHex(isViolation ? 0xff4444 : 0xcccccc);
    sphereMesh.setColorAt(i, color);
  }

  sphereMesh.instanceMatrix.needsUpdate = true;
  if (sphereMesh.instanceColor) sphereMesh.instanceColor.needsUpdate = true;

  if (prevShow && s >= 1) prevShow = null;
}

// ---------------------------------------------------------------------------
// Show selection and blending
// ---------------------------------------------------------------------------

function formatDuration(ms) {
  if (ms >= 1000) {
    const s = ms / 1000;
    return Number.isInteger(s) ? `${s}s` : `${s.toFixed(1)}s`;
  }
  return `${Math.round(ms)}ms`;
}

function layerSummary(show) {
  if (!show.n_layers) return '-';
  const parts = [];
  for (let i = 0; i < show.n_layers; i++) {
    const layer = show.layers[i];
    const op = layer.op || 'sine';
    parts.push(layer.axis ? `${op}(${layer.axis})` : op);
  }
  return parts.join(' + ');
}

function updateInfoPanel() {
  if (!currentShow) return;
  const show = currentShow;

  document.getElementById('grid-info').textContent = `${show.grid_cols}×${show.grid_rows}`;
  document.getElementById('ball-count').textContent = show.grid_cols * show.grid_rows;
  document.getElementById('duration-info').textContent = formatDuration(currentCycleMs);
  document.getElementById('layer-info').textContent = layerSummary(show);
  const fidelityText = currentFidelity === 'ideal'
    ? 'ideal'
    : `${currentFidelity}${fidelityExaggerate ? ' (error ×1000)' : ' (true scale)'}`;
  document.getElementById('fidelity-info').textContent = fidelityText;

  const load = show.coreHandle ? pc.busLoad(show.coreHandle) : 0;
  document.getElementById('bus-load').textContent = `${Math.max(0, load)}%`;

  document.getElementById('violation-count').textContent = currentReport.count;

  const banner = document.getElementById('violation-banner');
  const violationText = document.getElementById('violation-text');
  if (currentReport.count > 0) {
    banner.classList.add('visible');
    violationText.textContent = `${currentReport.count} violation(s) found`;
  } else {
    banner.classList.remove('visible');
  }
}

function updateShowButtons() {
  const wrap = document.getElementById('show-buttons');
  wrap.innerHTML = '';
  shows.forEach((show, i) => {
    const btn = document.createElement('button');
    btn.textContent = show.name;
    if (i === currentShowIndex) btn.classList.add('active');
    btn.addEventListener('click', () => selectShow(i));
    wrap.appendChild(btn);
  });
}

function selectShow(index, doBlend = true) {
  const next = shows[index];
  if (!next) return;
  try {
    ensureCoreShow(next);
  } catch (err) {
    alert('Could not load show into the motion core: ' + err.message);
    return;
  }

  const prev = currentShow;
  currentShowIndex = index;
  currentShow = next;
  currentCycleMs = computeLoopPeriodMs(next);
  currentReport = next.report || { count: 0, violations: [] };

  const sameGrid = prev && prev.grid_cols === next.grid_cols && prev.grid_rows === next.grid_rows;
  if (doBlend && sameGrid) {
    prevShow = prev;
    blendStartWall = performance.now();
  } else {
    prevShow = null;
  }

  ensureSpheres(next.grid_cols * next.grid_rows);
  updateShowButtons();
  updateInfoPanel();
}

// ---------------------------------------------------------------------------
// Animation loop
// ---------------------------------------------------------------------------

function setPlayingUI(playing) {
  document.getElementById('play-btn').classList.toggle('active', playing);
  document.getElementById('pause-btn').classList.toggle('active', !playing);
}

function animate(now) {
  requestAnimationFrame(animate);
  controls.update();

  if (lastFrameMs == null) lastFrameMs = now;
  let dt = now - lastFrameMs;
  lastFrameMs = now;
  if (dt < 0) dt = 0;
  if (dt > 100) dt = 100;

  if (isPlaying) {
    simTimeMs += dt * playSpeed;
    if (!isLooping) {
      const cycle = currentCycleMs || 1;
      if (simTimeMs >= cycle) {
        simTimeMs = cycle;
        isPlaying = false;
        setPlayingUI(false);
      }
    }
  }

  const cycle = currentCycleMs || 1;
  const progress = ((simTimeMs % cycle) + cycle) % cycle / cycle;
  document.getElementById('timeline-progress').style.width = `${progress * 100}%`;

  updateSpherePositions(simTimeMs, now);
  renderer.render(scene, camera);
}

// ---------------------------------------------------------------------------
// UI controls
// ---------------------------------------------------------------------------

document.getElementById('play-btn').addEventListener('click', () => {
  if (!currentShow) return;
  const cycle = currentCycleMs || 1;
  if (!isLooping && simTimeMs >= cycle) simTimeMs = 0;
  isPlaying = true;
  setPlayingUI(true);
});

document.getElementById('pause-btn').addEventListener('click', () => {
  isPlaying = false;
  setPlayingUI(false);
});

document.getElementById('loop-btn').addEventListener('click', () => {
  isLooping = !isLooping;
  document.getElementById('loop-btn').classList.toggle('active', isLooping);
  if (!isLooping) {
    const cycle = currentCycleMs || 1;
    if (simTimeMs > cycle) simTimeMs = cycle - 1;
  }
});

document.getElementById('speed-slider').addEventListener('input', (e) => {
  playSpeed = parseFloat(e.target.value);
  document.getElementById('speed-label').textContent = `${playSpeed.toFixed(1)}x`;
});

document.getElementById('fidelity-select').addEventListener('change', (e) => {
  currentFidelity = e.target.value;
  updateInfoPanel();
  updateSpherePositions(simTimeMs, performance.now());
});

document.getElementById('fidelity-exaggerate').addEventListener('change', (e) => {
  fidelityExaggerate = e.target.checked;
  updateInfoPanel();
  updateSpherePositions(simTimeMs, performance.now());
});

document.getElementById('timeline').addEventListener('click', (e) => {
  if (!currentShow) return;
  const rect = document.getElementById('timeline').getBoundingClientRect();
  const x = e.clientX - rect.left;
  const progress = Math.max(0, Math.min(1, x / rect.width));
  const cycle = currentCycleMs || 1;
  simTimeMs = progress * cycle;
  prevShow = null;
  document.getElementById('timeline-progress').style.width = `${progress * 100}%`;
  updateSpherePositions(simTimeMs, performance.now());
});

// ---------------------------------------------------------------------------
// Project pattern catalogue (Vite dev server)
// ---------------------------------------------------------------------------

function setPatternStatus(text) {
  const el = document.getElementById('pattern-status');
  if (el) el.textContent = text;
}

// ---------------------------------------------------------------------------
// Shareable render links (#pattern=<gzip|plain base64url>)
// ---------------------------------------------------------------------------

function bytesToBase64Url(bytes) {
  let bin = '';
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function base64UrlToBytes(text) {
  const padded = text.replace(/-/g, '+').replace(/_/g, '/');
  const bin = atob(padded + '='.repeat((4 - (padded.length % 4)) % 4));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

async function encodePatternPayload(text) {
  const bytes = new TextEncoder().encode(text);
  if (typeof CompressionStream !== 'undefined') {
    try {
      const stream = new Blob([bytes]).stream()
        .pipeThrough(new CompressionStream('gzip'));
      const packed = new Uint8Array(await new Response(stream).arrayBuffer());
      return `g1.${bytesToBase64Url(packed)}`;
    } catch {
      /* fall back to plain base64url */
    }
  }
  return `j1.${bytesToBase64Url(bytes)}`;
}

async function decodePatternPayload(payload) {
  if (payload.startsWith('g1.')) {
    if (typeof DecompressionStream === 'undefined') {
      throw new Error('this browser cannot decompress the pattern link');
    }
    const packed = base64UrlToBytes(payload.slice(3));
    const stream = new Blob([packed]).stream()
      .pipeThrough(new DecompressionStream('gzip'));
    return new TextDecoder().decode(await new Response(stream).arrayBuffer());
  }
  if (payload.startsWith('j1.')) {
    return new TextDecoder().decode(base64UrlToBytes(payload.slice(3)));
  }
  throw new Error('unknown pattern link format');
}

function patternTextForShare() {
  if (!currentShow) return null;
  ensureCoreShow(currentShow);
  if (currentShow.sourceText) return currentShow.sourceText;
  const duration = currentShow.duration_ms ||
    Math.max(computeLoopPeriodMs(currentShow), currentShow.dt_ms || 10);
  return showJson(currentShow, duration);
}

async function copyRenderLink() {
  const text = patternTextForShare();
  if (!text) return;
  if (location.protocol === 'file:') {
    setPatternStatus('Host the page to share render links');
    return;
  }
  const payload = await encodePatternPayload(text);
  const url = `${location.origin}${location.pathname}#pattern=${payload}`;
  try {
    await navigator.clipboard.writeText(url);
    setPatternStatus('Render link copied');
  } catch {
    prompt('Copy this render link:', url);
  }
}

async function loadPatternFromHash() {
  const match = location.hash.match(/^#pattern=(.+)$/);
  if (!match) return false;
  try {
    const text = await decodePatternPayload(match[1]);
    const handle = pc.load(text, 1);
    if (!handle) throw new Error(pc.lastError());
    const show = showFromHandle('Shared pattern', handle);
    show.sourceText = text;
    shows.push(show);
    selectShow(shows.length - 1, false);
    setPatternStatus('Shared pattern loaded');
    return true;
  } catch (err) {
    setPatternStatus(`Shared pattern: ${err.message}`);
    return false;
  }
}

document.getElementById('share-btn').addEventListener('click', copyRenderLink);

async function fetchPatternCatalog() {
  const res = await fetch('/api/patterns', { cache: 'no-store' });
  if (!res.ok) throw new Error('pattern API unavailable');
  const data = await res.json();
  return data.patterns || [];
}

function populatePatternSelect(patterns) {
  const select = document.getElementById('pattern-select');
  const key = patterns.map((p) => p.name).join('\n');
  if (key === patternNamesKey) return;
  patternNamesKey = key;

  const keep = selectedPatternName || select.value;
  select.innerHTML = '';
  const blank = document.createElement('option');
  blank.value = '';
  blank.textContent = patterns.length ? 'Choose a pattern...' : 'No patterns found';
  select.appendChild(blank);
  for (const pattern of patterns) {
    const option = document.createElement('option');
    option.value = pattern.name;
    option.textContent = pattern.name.replace(/\.anim\.json$/, '');
    select.appendChild(option);
  }
  select.value = patterns.some((p) => p.name === keep) ? keep : '';
}

async function postReport(name, reportText) {
  if (!reportText) return;
  try {
    await fetch('/api/report', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, report: reportText }),
    });
  } catch {
    /* the CLI can still write the report */
  }
}

async function loadCatalogPattern(name, activate = true) {
  if (!pc || patternLoading) return;
  patternLoading = true;
  try {
    const res = await fetch(`/api/pattern?name=${encodeURIComponent(name)}`,
                            { cache: 'no-store' });
    if (!res.ok) {
      setPatternStatus(`${name}: cannot read file`);
      return;
    }
    const data = await res.json();
    selectedPatternName = name;
    selectedPatternHash = data.hash;

    const handle = pc.load(data.text, 1);
    if (!handle) {
      setPatternStatus(`${name}: ${pc.lastError()}`);
      return;
    }

    const show = showFromHandle(name.replace(/\.anim\.json$/, ''), handle);
    show.sourceName = name;
    show.sourceText = data.text;
    const existing = shows.findIndex((s) => s.sourceName === name);
    if (existing >= 0) {
      shows[existing] = show;
      if (activate || currentShowIndex === existing)
        selectShow(existing, false);
    } else {
      shows.push(show);
      if (activate || currentShowIndex < 0)
        selectShow(shows.length - 1, false);
    }

    selectedPatternName = name;
    document.getElementById('pattern-select').value = name;
    localStorage.setItem('suspended-spheres-pattern', name);

    const report = show.report;
    setPatternStatus(report.count
      ? `${name}: ${report.count} violation(s)`
      : `${name}: clean`);
    await postReport(name, show.report_json);
  } catch (err) {
    setPatternStatus(`${name}: ${err.message}`);
  } finally {
    patternLoading = false;
  }
}

async function pollPatternCatalog() {
  if (!autoReloadPatterns || !selectedPatternName || patternLoading) return;
  try {
    const patterns = await fetchPatternCatalog();
    populatePatternSelect(patterns);
    const entry = patterns.find((p) => p.name === selectedPatternName);
    if (!entry) {
      setPatternStatus(`${selectedPatternName}: removed`);
      return;
    }
    if (entry.hash !== selectedPatternHash) {
      setPatternStatus(`${selectedPatternName}: changed, reloading...`);
      await loadCatalogPattern(selectedPatternName, false);
    }
  } catch {
    /* the Vite API disappears in a production build */
  }
}

async function initPatternBrowser() {
  const select = document.getElementById('pattern-select');
  const auto = document.getElementById('pattern-autoreload');

  select.addEventListener('change', (e) => {
    if (!e.target.value) return;
    selectedPatternName = e.target.value;
    loadCatalogPattern(e.target.value, true);
  });
  auto.addEventListener('change', (e) => {
    autoReloadPatterns = e.target.checked;
  });

  try {
    const patterns = await fetchPatternCatalog();
    populatePatternSelect(patterns);
    setPatternStatus(`${patterns.length} pattern(s) in patterns/`);

    const saved = localStorage.getItem('suspended-spheres-pattern');
    if (saved && patterns.some((p) => p.name === saved)) {
      select.value = saved;
      await loadCatalogPattern(saved, true);
    }
    patternPollTimer = setInterval(pollPatternCatalog, 500);
  } catch {
    select.innerHTML = '<option value="">Dev server only</option>';
    select.disabled = true;
    auto.disabled = true;
    setPatternStatus('Run npm run dev to watch patterns/');
  }
}

// ---------------------------------------------------------------------------
// File loading (adds a show to the list)
// ---------------------------------------------------------------------------

document.getElementById('pattern-file').addEventListener('change', async (event) => {
  const file = event.target.files[0];
  if (!file) return;
  try {
    const text = await file.text();
    const handle = pc.load(text, 1);
    if (!handle) {
      alert('Failed to parse pattern file: ' + pc.lastError());
      return;
    }
    const name = file.name.replace(/\.anim\.json$/, '').replace(/\.json$/, '') || 'Loaded';
    const show = showFromHandle(name, handle);
    show.sourceText = text;
    shows.push(show);
    selectShow(shows.length - 1);
    selectedPatternName = '';
    selectedPatternHash = '';
    localStorage.removeItem('suspended-spheres-pattern');
    document.getElementById('pattern-select').value = '';
    setPatternStatus('Manual file loaded');
  } catch (err) {
    alert('Error loading pattern: ' + err.message);
  }
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

async function initViewer() {
  try {
    await initCore();
  } catch (err) {
    alert('Could not load the shared motion core: ' + err.message);
    return;
  }
  updateShowButtons();
  const shared = await loadPatternFromHash();
  if (!shared) selectShow(1, false); // start on the X/Y showcase
  requestAnimationFrame(animate);
  initPatternBrowser();
}

initViewer();

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});
