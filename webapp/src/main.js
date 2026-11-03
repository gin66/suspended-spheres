import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// Pattern file structures (from test_pattern.cpp)
const MAX_LAYERS = 16;
const MAX_SAMPLES = 20000;

// ---------------------------------------------------------------------------
// Pattern file schema / parsing (parity with tests/test_pattern.cpp)
// ---------------------------------------------------------------------------

class LayerDef {
  constructor() {
    this.op = '';
    this.amplitude_um = 0;
    this.period_ms = 0;
    this.wavelength_cols = 0;
    this.axis = '';
    this.phase_rad = 0;
  }
}

class PatternFile {
  constructor() {
    this.version = 0;
    this.grid = [0, 0];
    this.grid_cols = 0;
    this.grid_rows = 0;
    this.dt_ms = 0;
    this.n = 0;
    this.duration_ms = 0;
    this.tier = '';
    this.n_layers = 0;
    this.layers = [];
    for (let i = 0; i < MAX_LAYERS; i++) {
      this.layers.push(new LayerDef());
    }
  }
}

class TrajectorySample {
  constructor() {
    this.ball_index = 0;
    this.time_ms = 0;
    this.pos_um = 0;
  }
}

class Trajectory {
  constructor() {
    this.samples = [];
    this.count = 0;
    this.grid_cols = 0;
    this.grid_rows = 0;
    this.dt_ms = 0;
    this.n = 0;
    this.duration_ms = 0;
  }
}

class LimitViolation {
  constructor() {
    this.ball_index = 0;
    this.time_ms = 0;
    this.limit_name = '';
    this.measured_um = 0;
    this.allowed_um = 0;
  }
}

class LimitReport {
  constructor() {
    this.violations = [];
    this.count = 0;
  }
}

function parse_pattern_file(text) {
  const pf = new PatternFile();
  let data;
  try { data = JSON.parse(text); } catch { return null; }
  if (!data || typeof data !== 'object') return null;

  if (typeof data.version !== 'number' || data.version !== Math.floor(data.version) || data.version !== 1) return null;
  pf.version = data.version;

  if (!Array.isArray(data.grid) || data.grid.length !== 2) return null;
  const g0 = data.grid[0], g1 = data.grid[1];
  if (typeof g0 !== 'number' || typeof g1 !== 'number') return null;
  pf.grid[0] = g0; pf.grid[1] = g1;
  if (!((pf.grid[0] === 10 && pf.grid[1] === 10) || (pf.grid[0] === 10 && pf.grid[1] === 12))) return null;
  pf.grid_cols = pf.grid[0];
  pf.grid_rows = pf.grid[1];

  if (typeof data.dt_ms !== 'number') return null;
  pf.dt_ms = data.dt_ms;
  if (pf.dt_ms !== 10 && pf.dt_ms !== 11 && pf.dt_ms !== 15) return null;

  if (typeof data.n !== 'number') return null;
  pf.n = data.n;
  if (pf.n !== 3 && pf.n !== 4) return null;

  if (typeof data.duration_ms !== 'number') return null;
  pf.duration_ms = data.duration_ms;

  if (typeof data.tier !== 'string') return null;
  pf.tier = data.tier;

  if (!Array.isArray(data.layers)) return null;

  pf.n_layers = 0;
  for (let i = 0; i < data.layers.length && pf.n_layers < MAX_LAYERS; i++) {
    const lay = data.layers[i];
    if (typeof lay !== 'object' || lay === null) return null;
    const layer = pf.layers[pf.n_layers];

    if (typeof lay.op !== 'string') return null;
    layer.op = lay.op;
    if (typeof lay.amplitude_um !== 'number') return null;
    layer.amplitude_um = lay.amplitude_um;
    if (typeof lay.period_ms !== 'number') return null;
    layer.period_ms = lay.period_ms;
    if (typeof lay.wavelength_cols !== 'number') return null;
    layer.wavelength_cols = lay.wavelength_cols;
    if (typeof lay.axis !== 'string') return null;
    layer.axis = lay.axis;
    layer.phase_rad = typeof lay.phase_rad === 'number' ? lay.phase_rad : 0;

    pf.n_layers++;
  }

  return pf;
}

// ---------------------------------------------------------------------------
// Kinematics: additive layer composition
// ---------------------------------------------------------------------------

// One sine layer contribution around the 1 m centre. axis "col" varies with
// the grid column, "row" with the grid row, so two layers make X/Y waves.
function sineContributionUm(lay, col, row, time_ms) {
  const coord = lay.axis === 'row' ? row : col;
  const period = lay.period_ms > 0 ? lay.period_ms : 1;
  const wl = lay.wavelength_cols > 0 ? lay.wavelength_cols : 1;
  const phase = (2.0 * Math.PI * time_ms) / period + (lay.phase_rad || 0);
  const spatial = (2.0 * Math.PI * coord) / wl;
  return (lay.amplitude_um || 0) * Math.sin(phase + spatial);
}

function evaluatePositionUm(show, col, row, time_ms) {
  let um = 1000000;
  for (let l = 0; l < show.n_layers; l++) {
    um += sineContributionUm(show.layers[l], col, row, time_ms);
  }
  if (um < 0) um = 0;
  else if (um > 2000000) um = 2000000;
  return um;
}

function bake_trajectory(show) {
  const tr = new Trajectory();
  const total_balls = show.grid_cols * show.grid_rows;
  const n_samples = show.duration_ms / show.dt_ms;

  if (n_samples * total_balls > MAX_SAMPLES) return null;

  tr.grid_cols = show.grid_cols;
  tr.grid_rows = show.grid_rows;
  tr.dt_ms = show.dt_ms;
  tr.n = show.n;
  tr.duration_ms = show.duration_ms;
  tr.count = 0;

  for (let t = 0; t < n_samples; t++) {
    const time_ms = t * show.dt_ms;
    for (let ball = 0; ball < total_balls; ball++) {
      const col = ball % show.grid_cols;
      const row = Math.floor(ball / show.grid_cols);
      const s = new TrajectorySample();
      s.ball_index = ball;
      s.time_ms = time_ms;
      s.pos_um = evaluatePositionUm(show, col, row, time_ms);
      tr.samples[tr.count] = s;
      tr.count++;
    }
  }
  return tr;
}

function check_limits(tr, tier) {
  const report = new LimitReport();
  const total_balls = tr.grid_cols * tr.grid_rows;
  const n_samples = tr.duration_ms / tr.dt_ms;

  for (let ball = 0; ball < total_balls; ball++) {
    for (let t = 0; t < n_samples; t++) {
      const idx = t * total_balls + ball;
      if (idx >= tr.count) break;
      const s = tr.samples[idx];
      if (s.pos_um < 0 || s.pos_um > 2000000) {
        const v = new LimitViolation();
        v.ball_index = ball;
        v.time_ms = s.time_ms;
        v.limit_name = 'FR-MOT-005';
        v.measured_um = s.pos_um;
        v.allowed_um = 2000000;
        report.violations[report.count] = v;
        report.count++;
      }
      if (t > 0 && t < n_samples - 1) {
        const idx_prev = (t - 1) * total_balls + ball;
        const idx_next = (t + 1) * total_balls + ball;
        if (idx_prev >= 0 && idx_prev < tr.count && idx_next >= 0 && idx_next < tr.count) {
          const dz = tr.samples[idx_next].pos_um - tr.samples[idx_prev].pos_um;
          const speed_um_s = Math.abs(dz) * 1000 / (tr.dt_ms * 2);
          let speed_limit = 0;
          if (tier === 'ordinary') speed_limit = 300000;
          else if (tier === 'nominal') speed_limit = 590000;
          else if (tier === 'optional') speed_limit = 1300000;
          if (speed_um_s > speed_limit) {
            const v = new LimitViolation();
            v.ball_index = ball;
            v.time_ms = s.time_ms;
            v.limit_name = 'ASM-015';
            v.measured_um = speed_um_s;
            v.allowed_um = speed_limit;
            report.violations[report.count] = v;
            report.count++;
          }
        }
      }
    }
  }
  return report;
}

// Motion core conversions (from motion_core.h)
function mc_code_to_um(code) { return Math.round((code * 2000000) / 65535); }
function mc_um_to_code(um) { return Math.round((um * 65535) / 2000000); }
function mc_um_to_steps(um) { return Math.round((um * 1668861) / (1000 * 65536)); }
function mc_steps_to_um(steps) { return Math.round((steps * 1000 * 65536) / 1668861); }

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
    op: 'sine',
    amplitude_um: l.amplitude_um,
    period_ms: l.period_ms,
    wavelength_cols: l.wavelength_cols,
    axis: l.axis,
    phase_rad: l.phase_rad || 0,
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
        ? mc_code_to_um(mc_um_to_code(um))
        : mc_steps_to_um(mc_um_to_steps(um));
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
    parts.push(`sine(${show.layers[i].axis})`);
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

  const frames_s = Math.round(120000 / (show.n * show.dt_ms));
  const load = Math.round((frames_s + 350) * 130 / 10000);
  document.getElementById('bus-load').textContent = `${load}%`;

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
// File loading (adds a show to the list)
// ---------------------------------------------------------------------------

document.getElementById('pattern-file').addEventListener('change', async (event) => {
  const file = event.target.files[0];
  if (!file) return;
  try {
    const text = await file.text();
    const pf = parse_pattern_file(text);
    if (!pf) {
      alert('Failed to parse pattern file');
      return;
    }

    const show = {
      name: file.name.replace(/\.anim\.json$/, '').replace(/\.json$/, '') || 'Loaded',
      version: pf.version,
      grid: [pf.grid_cols, pf.grid_rows],
      grid_cols: pf.grid_cols,
      grid_rows: pf.grid_rows,
      dt_ms: pf.dt_ms,
      n: pf.n,
      duration_ms: pf.duration_ms,
      tier: pf.tier,
      layers: pf.layers.slice(0, pf.n_layers),
      n_layers: pf.n_layers,
    };

    const tr = bake_trajectory(show);
    if (!tr) {
      alert('Trajectory too large');
      return;
    }
    show.report = check_limits(tr, show.tier);

    shows.push(show);
    selectShow(shows.length - 1);
  } catch (err) {
    alert('Error loading pattern: ' + err.message);
  }
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

updateShowButtons();
selectShow(1, false); // start on the X/Y showcase
requestAnimationFrame(animate);

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});
