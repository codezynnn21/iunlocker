const presets = {
  adreno830: { renderer: 'Adreno (TM) 830', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0762.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno810: { renderer: 'Adreno (TM) 810', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0750.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno750: { renderer: 'Adreno (TM) 750', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0686.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno740: { renderer: 'Adreno (TM) 740', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0676.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno730: { renderer: 'Adreno (TM) 730', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0615.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno725: { renderer: 'Adreno (TM) 725', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0615.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno720: { renderer: 'Adreno (TM) 720', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0615.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno710: { renderer: 'Adreno (TM) 710', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0530.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno660: { renderer: 'Adreno (TM) 660', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0530.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno650: { renderer: 'Adreno (TM) 650', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno642: { renderer: 'Adreno (TM) 642L', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno640: { renderer: 'Adreno (TM) 640', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno630: { renderer: 'Adreno (TM) 630', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0490.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno620: { renderer: 'Adreno (TM) 620', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno619: { renderer: 'Adreno (TM) 619', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno618: { renderer: 'Adreno (TM) 618', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0500.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno616: { renderer: 'Adreno (TM) 616', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0490.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno612: { renderer: 'Adreno (TM) 612', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0490.0', shading: 'OpenGL ES GLSL ES 3.20' },
  adreno610: { renderer: 'Adreno (TM) 610', vendor: 'Qualcomm', version: 'OpenGL ES 3.2 V@0490.0', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG720: { renderer: 'Mali-G720', vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r44p1', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG715: { renderer: 'Mali-G715', vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r40p0', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG710: { renderer: 'Mali-G710', vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r38p1', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG78:  { renderer: 'Mali-G78',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r32p1', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG77:  { renderer: 'Mali-G77',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r28p0', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG76:  { renderer: 'Mali-G76',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r16p0', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG72:  { renderer: 'Mali-G72',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r12p0', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG68:  { renderer: 'Mali-G68',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r32p1', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG57:  { renderer: 'Mali-G57',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r32p1', shading: 'OpenGL ES GLSL ES 3.20' },
  maliG52:  { renderer: 'Mali-G52',  vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r25p1', shading: 'OpenGL ES GLSL ES 3.20' },
  immortalisG720: { renderer: 'Immortalis-G720', vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r44p1', shading: 'OpenGL ES GLSL ES 3.20' },
  immortalisG715: { renderer: 'Immortalis-G715', vendor: 'ARM', version: 'OpenGL ES 3.2 v1.r40p0', shading: 'OpenGL ES GLSL ES 3.20' },
  xclipse940: { renderer: 'Samsung Xclipse 940', vendor: 'Samsung', version: 'OpenGL ES 3.2', shading: 'OpenGL ES GLSL ES 3.20' },
  xclipse920: { renderer: 'Samsung Xclipse 920', vendor: 'Samsung', version: 'OpenGL ES 3.2', shading: 'OpenGL ES GLSL ES 3.20' },
  powervr: { renderer: 'PowerVR Rogue GE8320', vendor: 'Imagination', version: 'OpenGL ES 3.2', shading: 'OpenGL ES GLSL ES 3.20' },
  maleoon910: { renderer: 'Maleoon 910', vendor: 'Huawei', version: 'OpenGL ES 3.2', shading: 'OpenGL ES GLSL ES 3.20' }
};

const ramOptions = [2, 3, 4, 6, 8, 10, 12, 16, 18, 24];

const DEFAULT_CFG = {
  enabled: true,
  renderer: 'Adreno (TM) 740',
  vendor: 'Qualcomm',
  version: 'OpenGL ES 3.2',
  shading: 'OpenGL ES GLSL ES 3.20',
  ram: 8,
  props: true,
  paths: true
};

const $ = (id) => document.getElementById(id);

let state = { games: {}, active: null };
let installed = [];   // packages from pm list
let filterText = '';

function defaultGames() {
  return {};
}

function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 1600);
}

function setStatus(msg) { $('status').textContent = msg; }

async function detectRoot() {
  const badge = $('rootBadge');
  if (!badge) return false;
  badge.textContent = 'checking';
  badge.className = 'root-badge checking';

  let rooted = false;
  try {
    if (window.ksu?.exec) {
      // KernelSU WebUI — try a privileged command
      const out = await window.ksu.exec('id');
      if (out && (out.includes('uid=0') || out.includes('root'))) rooted = true;
      if (!rooted) {
        const adb = await window.ksu.exec('ls /data/adb 2>/dev/null | head -1');
        if (adb && adb.trim().length) rooted = true;
      }
    } else if (typeof window.$magisk !== 'undefined' || typeof window.magisk !== 'undefined') {
      rooted = true;
    } else if (window.Android && typeof window.Android.exec === 'function') {
      try {
        const out = window.Android.exec('id');
        if (out && String(out).includes('uid=0')) rooted = true;
      } catch (_) {}
    }
  } catch (_) {
    rooted = false;
  }

  if (rooted) {
    badge.textContent = 'rooted';
    badge.className = 'root-badge rooted';
  } else {
    badge.textContent = 'not rooted';
    badge.className = 'root-badge not-rooted';
  }
  return rooted;
}



/* ---------- GPU / RAM presets ---------- */
function familyOf(key, p) {
  const r = (p.renderer || '').toLowerCase();
  if (r.includes('adreno')) return 'Adreno';
  if (r.includes('immortalis')) return 'Immortalis';
  if (r.includes('mali')) return 'Mali';
  if (r.includes('xclipse') || r.includes('samsung')) return 'Samsung';
  if (r.includes('powervr')) return 'PowerVR';
  if (r.includes('maleoon')) return 'Huawei';
  return 'Other';
}

const FAMILY_ORDER = ['Adreno', 'Mali', 'Immortalis', 'Samsung', 'PowerVR', 'Huawei', 'Other'];
let activeFamily = 'Adreno';
let activeGpuKey = '';

function buildFamilyMap() {
  const groups = {};
  Object.keys(presets).forEach(key => {
    const fam = familyOf(key, presets[key]);
    (groups[fam] || (groups[fam] = [])).push(key);
  });
  return groups;
}

function shortLabel(renderer) {
  return renderer
    .replace(' (TM)', '')
    .replace('PowerVR Rogue ', 'PowerVR ')
    .replace('Samsung ', '')
    .replace('Immortalis-', 'Imm. ')
    .replace('Mali-', 'Mali ');
}

function applyGpuKey(key) {
  if (!key || !presets[key]) return;
  activeGpuKey = key;
  const p = presets[key];
  $('renderer').value = p.renderer;
  $('vendor').value = p.vendor;
  $('version').value = p.version;
  $('shading').value = p.shading;
  const chosen = $('gpuChosen');
  if (chosen) chosen.textContent = p.renderer;
  // refresh model chips active state
  document.querySelectorAll('#gpuModels .chip').forEach(c => {
    c.classList.toggle('active', c.dataset.key === key);
  });
}

function renderGpuModels() {
  const groups = buildFamilyMap();
  const box = $('gpuModels');
  box.innerHTML = '';
  const keys = groups[activeFamily] || [];
  if (!keys.length) {
    box.innerHTML = '<p class="hint" style="margin:0">no models</p>';
    return;
  }
  keys.forEach(key => {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'chip' + (key === activeGpuKey ? ' active' : '');
    btn.dataset.key = key;
    btn.textContent = shortLabel(presets[key].renderer);
    btn.onclick = () => applyGpuKey(key);
    box.appendChild(btn);
  });
}

function populatePresets() {
  const famBox = $('gpuFamily');
  famBox.innerHTML = '';
  const groups = buildFamilyMap();
  FAMILY_ORDER.forEach(fam => {
    if (!groups[fam] || !groups[fam].length) return;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'seg-btn' + (fam === activeFamily ? ' active' : '');
    btn.textContent = fam;
    btn.onclick = () => {
      activeFamily = fam;
      famBox.querySelectorAll('.seg-btn').forEach(b => b.classList.toggle('active', b.textContent === fam));
      renderGpuModels();
    };
    famBox.appendChild(btn);
  });
  renderGpuModels();

  const ramBox = $('ramPresets');
  ramBox.innerHTML = '';
  ramOptions.forEach(gb => {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'chip';
    btn.textContent = `${gb} GB`;
    btn.onclick = () => {
      $('ram').value = gb;
      updateRamLabel();
      ramBox.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c.textContent === `${gb} GB`));
    };
    ramBox.appendChild(btn);
  });
}

function updateRamLabel() {
  $('ramLabel').textContent = `${parseInt($('ram').value, 10) || 8} GB`;
}

/* ---------- picker: installed packages ---------- */
function renderPicker() {
  const box = $('pkgPicker');
  const hint = $('pickerHint');
  const q = filterText.toLowerCase().trim();

  let list = installed.slice();
  if (q) list = list.filter(p => p.toLowerCase().includes(q));

  // also show already-configured packages that match filter even if not in scan
  Object.keys(state.games).forEach(p => {
    if (!list.includes(p) && (!q || p.toLowerCase().includes(q))) list.push(p);
  });
  list.sort();

  box.innerHTML = '';
  if (!list.length) {
    const p = document.createElement('p');
    p.className = 'hint';
    p.id = 'pickerHint';
    p.textContent = installed.length
      ? 'No match — try another filter or type package below.'
      : 'Tap Scan installed, or type a package name below.';
    box.appendChild(p);
    return;
  }

  // limit DOM size
  const max = 80;
  list.slice(0, max).forEach(pkg => {
    const row = document.createElement('div');
    const isCfg = !!state.games[pkg];
    const isActive = pkg === state.active;
    row.className = 'pkg-item' + (isActive ? ' active' : '') + (isCfg ? ' configured' : '');
    row.innerHTML = `
      <div class="pkg-name">${pkg}</div>
      <div class="pkg-badge">${isCfg ? (state.games[pkg].enabled !== false ? 'on' : 'off') : 'add'}</div>
    `;
    row.onclick = () => pickPackage(pkg);
    box.appendChild(row);
  });
  if (list.length > max) {
    const more = document.createElement('p');
    more.className = 'hint';
    more.textContent = `…and ${list.length - max} more — refine filter`;
    box.appendChild(more);
  }
}

function pickPackage(pkg) {
  if (!state.games[pkg]) {
    state.games[pkg] = { ...DEFAULT_CFG };
    persist(); // keep it
  }
  selectGame(pkg);
  toast(pkg.split('.').pop());
}

/* ---------- configured list ---------- */
function renderGameList() {
  const card = $('configuredCard');
  const box = $('gameList');
  const pkgs = Object.keys(state.games);
  if (!pkgs.length) {
    card.style.display = 'none';
    return;
  }
  card.style.display = 'block';
  box.innerHTML = '';
  pkgs.sort().forEach(pkg => {
    const g = state.games[pkg];
    const row = document.createElement('div');
    row.className = 'game-item' + (pkg === state.active ? ' active' : '');
    row.innerHTML = `
      <div class="game-meta">
        <div class="game-pkg">${pkg}</div>
        <div class="game-sub">${g.renderer || '—'} · ${g.ram || 8} GB · ${g.enabled !== false ? 'on' : 'off'}</div>
      </div>
    `;
    row.onclick = () => selectGame(pkg);
    box.appendChild(row);
  });
}

/* ---------- editor ---------- */
function selectGame(pkg) {
  state.active = pkg;
  const g = state.games[pkg] || { ...DEFAULT_CFG };
  $('editTitle').textContent = pkg;
  $('enabled').checked = g.enabled !== false;
  $('renderer').value = g.renderer || '';
  $('vendor').value = g.vendor || '';
  $('version').value = g.version || '';
  $('shading').value = g.shading || '';
  $('props').checked = g.props !== false;
  $('paths').checked = g.paths !== false;
  $('ram').value = g.ram || 8;
  updateRamLabel();

  // sync family + model chips to saved renderer
  activeGpuKey = '';
  Object.keys(presets).forEach(k => {
    if (presets[k].renderer === (g.renderer || '')) {
      activeGpuKey = k;
      activeFamily = familyOf(k, presets[k]);
    }
  });
  document.querySelectorAll('#gpuFamily .seg-btn').forEach(b => {
    b.classList.toggle('active', b.textContent === activeFamily);
  });
  renderGpuModels();
  const chosen = $('gpuChosen');
  if (chosen) chosen.textContent = g.renderer || 'none selected';

  $('editor').style.display = 'block';
  renderGameList();
  renderPicker();
  $('editor').scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function collectCurrent() {
  return {
    enabled: $('enabled').checked,
    renderer: $('renderer').value.trim(),
    vendor: $('vendor').value.trim(),
    version: $('version').value.trim(),
    shading: $('shading').value.trim(),
    props: $('props').checked,
    paths: $('paths').checked,
    ram: parseInt($('ram').value, 10) || 8,
    updatedAt: new Date().toISOString()
  };
}

/* ---------- persistence ---------- */
async function persist() {
  const payload = { games: state.games, updatedAt: new Date().toISOString() };
  localStorage.setItem('iunlocker_config', JSON.stringify(payload));
  if (window.ksu?.exec) {
    const json = JSON.stringify(payload, null, 2);
    const escaped = json.replace(/'/g, "'\\''");
    await window.ksu.exec(
      `mkdir -p /data/adb/iunlocker && printf '%s\\n' '${escaped}' > /data/adb/iunlocker/config.json`
    );
  }
}

async function load() {
  if (window.ksu?.exec) {
    try {
      const out = await window.ksu.exec('cat /data/adb/iunlocker/config.json 2>/dev/null');
      if (out && out.trim().startsWith('{')) {
        const parsed = JSON.parse(out);
        if (parsed.games) {
          state.games = parsed.games;
          localStorage.setItem('iunlocker_config', JSON.stringify(parsed));
          return;
        }
      }
    } catch (_) {}
  }
  try {
    const s = JSON.parse(localStorage.getItem('iunlocker_config') || '{}');
    if (s.games && Object.keys(s.games).length) {
      state.games = s.games;
      return;
    }
    const legacy = JSON.parse(localStorage.getItem('iunlocker_gpu') || 'null');
    if (legacy) {
      state.games = { 'com.garena.game.codm': { ...DEFAULT_CFG, ...legacy } };
      return;
    }
  } catch (_) {}
  state.games = defaultGames();
}

/* ---------- scan packages via ksu ---------- */
async function scanPackages() {
  setStatus('scanning…');
  if (!window.ksu?.exec) {
    setStatus('no ksu — type package manually');
    toast('ksu not available');
    return;
  }
  try {
    // third-party first, then all if empty
    let out = await window.ksu.exec('pm list packages -3 2>/dev/null');
    let pkgs = (out || '')
      .split('\n')
      .map(l => l.replace(/^package:/, '').trim())
      .filter(Boolean);

    if (pkgs.length < 5) {
      out = await window.ksu.exec('pm list packages 2>/dev/null');
      pkgs = (out || '')
        .split('\n')
        .map(l => l.replace(/^package:/, '').trim())
        .filter(Boolean);
    }

    // prefer game-ish names near top of filter results (no hard filter — user searches)
    installed = [...new Set(pkgs)].sort();
    setStatus(`found ${installed.length} packages`);
    toast(`${installed.length} packages`);
    renderPicker();
  } catch (e) {
    setStatus('scan failed');
    toast('scan failed');
  }
}

/* ---------- wire events ---------- */
$('ram').oninput = updateRamLabel;

$('pkgFilter').oninput = (e) => {
  filterText = e.target.value || '';
  renderPicker();
};

$('scanBtn').onclick = () => scanPackages();
$('refreshBtn').onclick = () => { renderPicker(); renderGameList(); toast('refreshed'); };

$('addGameBtn').onclick = () => {
  const pkg = ($('newPkg').value || '').trim();
  if (!pkg || !pkg.includes('.')) {
    toast('valid package name needed');
    return;
  }
  pickPackage(pkg);
  $('newPkg').value = '';
};

$('saveBtn').onclick = async () => {
  if (!state.active) return;
  setStatus('saving…');
  state.games[state.active] = collectCurrent();
  await persist();
  renderGameList();
  renderPicker();
  setStatus('saved — relaunch the game');
  toast('Saved');
};

$('deleteBtn').onclick = async () => {
  if (!state.active) return;
  const pkg = state.active;
  delete state.games[pkg];
  state.active = null;
  $('editor').style.display = 'none';
  await persist();
  renderGameList();
  renderPicker();
  setStatus('removed ' + pkg);
  toast('Removed');
};

/* ---------- boot ---------- */
populatePresets();
load().then(async () => {
  const rooted = await detectRoot();
  renderGameList();
  renderPicker();
  const n = Object.keys(state.games).length;
  if (n === 1) {
    selectGame(Object.keys(state.games)[0]);
  } else {
    state.active = null;
    $('editor').style.display = 'none';
  }
  setStatus(
    (rooted ? 'rooted' : 'not rooted') +
    ' · ' +
    (n ? (n + ' configured') : 'pick a game')
  );
  if (rooted && window.ksu?.exec) scanPackages();
});
