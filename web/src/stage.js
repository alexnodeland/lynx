// SPDX-License-Identifier: MIT
// The shared lynx scene: renderer, workshop lighting, the bench set, the product with its pan/tilt
// rig and per-part holders, the cables (rebuilt from CAD waypoints), range arcs and post-processing.
// Used by the landing page (main.js) and the build guide (build.js), so both show the same model.
import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { DRACOLoader } from "three/examples/jsm/loaders/DRACOLoader.js";
import { HDRLoader } from "three/examples/jsm/loaders/HDRLoader.js";
import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/examples/jsm/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
import { SMAAPass } from "three/examples/jsm/postprocessing/SMAAPass.js";
import { N8AOPass } from "n8ao";
import { restyle, cableMaterial } from "./materials.js";
import { DepthOfFieldPass } from "./dof.js";
import { siliconeMat, addLegs, MAT_THICKNESS } from "./bench.js";
import { solderingStation, ironHolder, ironCable, spool, caliper } from "./props.js";

export const MM = 0.001;
// Blender (z-up, mm) -> three (y-up, m)
export const b2t = (x, y, z) => new THREE.Vector3(x * MM, z * MM, -y * MM);      // world, metres
export const b2l = (x, y, z) => new THREE.Vector3(x, z, -y);                       // under Root: millimetres
export const REST_TILT = 45;   // at rest the lens leans 45 deg toward the front (0 = straight up)

export async function createStage({ canvas, onProgress } = {}) {
  // ------------------------------------------------------------------ renderer + scene
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: "high-performance" });
  // phones: fewer pixels and no AO/depth-of-field, so every frame lands inside the scroll's frame budget
  const TOUCH = matchMedia("(pointer: coarse)").matches;
  const MAX_RATIO = TOUCH ? 1.25 : 1.5;
  renderer.toneMapping = THREE.NeutralToneMapping;       // keeps the mat's blue and the oak's warmth saturated
  renderer.toneMappingExposure = 0.85;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;        // filtered over shadow.radius
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(30, (canvas.clientWidth || innerWidth) / (canvas.clientHeight || innerHeight), 0.005, 5);

  // Light comes from the workshop itself: an HDRI of a real one (Poly Haven, CC0) lights and reflects
  // in everything, and shows, out of focus, behind the bench. Its windows are turned to the back left,
  // and the key is soft daylight from the left.
  const ROOM_TURN = 2.9;                             // radians about y: the windows to the back left
  scene.environmentRotation.y = scene.backgroundRotation.y = ROOM_TURN;
  scene.environmentIntensity = 0.9;
  scene.backgroundBlurriness = 0.32;
  scene.backgroundIntensity = 0.2;                   // the room stays darker than the bench
  const key = new THREE.DirectionalLight(0xfff1e4, 2.2);
  key.position.set(-0.45, 0.4, 0.15);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  Object.assign(key.shadow.camera, { left: -0.2, right: 0.2, top: 0.2, bottom: -0.2, near: 0.05, far: 1.5 });
  key.shadow.bias = -0.0003;
  key.shadow.normalBias = 0.0004;
  key.shadow.radius = 5;
  scene.add(key);

  // ------------------------------------------------------------------ loading
  const manager = new THREE.LoadingManager();
  manager.onProgress = (_, done, total) => onProgress?.(done / total);
  const draco = new DRACOLoader(manager).setDecoderPath("./draco/");
  const gltf = new GLTFLoader(manager).setDRACOLoader(draco);
  const SCANS = ["multimeter", "pliers", "tape"];   // Poly Haven CC0 scans, public/assets/props
  const [lynx, set, rig, env, scans] = await Promise.all([
    gltf.loadAsync("./assets/lynx.glb"),
    gltf.loadAsync("./assets/set.glb"),
    fetch("./assets/rig.json").then((r) => r.json()),
    new HDRLoader(manager).loadAsync("./assets/workshop_512.hdr"),
    Promise.all(SCANS.map((n) => gltf.loadAsync(`./assets/props/${n}.glb`))),
    // the mat's and the base's print is drawn with the page's fonts
    document.fonts.load('600 16px "Martian Mono"'), document.fonts.load("800 16px Archivo"),
  ]);
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = scene.background = pmrem.fromEquirectangular(env).texture;
  env.dispose();
  pmrem.dispose();

  const setMats = restyle(set.scene);
  const anisotropy = renderer.capabilities.getMaxAnisotropy();   // the mat and desk are seen at grazing angles
  // the room is the HDRI now, and the flat ESD mat gives way to the silicone one
  for (const n of ["Wall", "ESDMat"]) set.scene.getObjectByName(n).visible = false;
  const mat = siliconeMat({ anisotropy });
  mat.position.set(0.02, set.scene.getObjectByName("Desk").position.y, -0.03);
  set.scene.add(mat);
  // the chips and caliper lay on the old mat: put them on the new one (it is a little thicker)
  const lift = mat.position.y + MAT_THICKNESS - set.scene.getObjectByName("ESDMat").position.y;
  set.scene.traverse((o) => { if (/^(Chip|Caliper)/.test(o.name)) o.position.y += lift; });
  for (const n of ["Chip0", "Chip1", "Chip2"]) addLegs(set.scene.getObjectByName(n), setMats.get("solder"));

  // The props, at product-shot detail (props.js), stand in for set.glb's placeholder blocks, on the desk.
  const deskY = set.scene.getObjectByName("Desk").position.y;
  set.scene.traverse((o) => { if (/^(Station|Iron|SolderSpool|WireSpool|Caliper)/.test(o.name)) o.visible = false; });
  const calip = caliper();
  calip.position.set(0.13, mat.position.y + MAT_THICKNESS, 0.06);
  calip.rotation.y = 0.24;
  set.scene.add(calip);
  const station = solderingStation();
  station.position.set(0.62, deskY, -0.64);
  station.rotation.y = -0.35;
  const holder = ironHolder();
  holder.position.set(0.715, deskY, -0.52);
  holder.rotation.y = -0.5;
  set.scene.add(station, holder);
  set.scene.updateMatrixWorld(true);
  set.scene.add(ironCable(holder.userData.cableStart(), station.localToWorld(station.userData.socket.clone()), deskY));
  const SPOOLS = [
    { at: [-0.25, 0, -0.17], radius: 0.032, height: 0.034, core: 0.0285, hole: 0.011, flange: 0x1f8c47, wire: "solder", turns: 34,
      label: ["SOLDER WIRE", "Sn63 Pb37 · ø0.8 mm", "250 g · rosin core"], band: "#1f8c47" },
    { at: [-0.32, 0, -0.07], radius: 0.045, height: 0.026, core: 0.04, hole: 0.012, flange: 0xe6e6e2, wire: "copper", turns: 26,
      label: ["ENAMELLED COPPER", "0.5 mm · 24 AWG", "500 g"], band: "#c8743e" },
    { at: [-0.32, 0.026, -0.07], radius: 0.045, height: 0.026, core: 0.04, hole: 0.012, flange: 0xe6e6e2, wire: "blue", turns: 18,
      label: ["HOOK-UP WIRE", "22 AWG · stranded", "30 m · blue"], band: "#1f45a6" },
    { at: [-0.21, 0, -0.29], radius: 0.05, height: 0.03, core: 0.045, hole: 0.013, flange: 0xe6e6e2, wire: "copper", turns: 40,
      label: ["ENAMELLED COPPER", "0.3 mm · 28 AWG", "250 g"], band: "#c8743e" },
  ];
  // scanned props (Poly Haven, CC0): an analogue multimeter and a tape measure at the back left with the
  // spools, out of focus behind the product, and pliers on the mat, clear of the product's shots
  const matTop = mat.position.y + MAT_THICKNESS;
  const PLACE = {
    multimeter: { at: [-0.46, deskY, -0.5], turn: 0.55 },
    pliers: { at: [0.215, matTop + 0.009, -0.035], turn: 0.55, lie: true },
    tape: { at: [-0.1, deskY, -0.31], turn: 0.5 },
  };
  SCANS.forEach((n, i) => {
    const { at, turn, lie } = PLACE[n], s = scans[i].scene;
    s.traverse((o) => { if (o.isMesh) o.castShadow = o.receiveShadow = true; });
    const prop = new THREE.Group();
    if (lie) s.rotation.x = -Math.PI / 2;            // scanned standing up: lay it on its side
    prop.add(s);
    prop.position.set(...at);
    prop.rotation.y = turn;
    set.scene.add(prop);
  });
  SPOOLS.forEach(({ at: [x, y, z], ...spec }, i) => {
    const s = spool(spec);
    s.position.set(x, deskY + y, z);
    s.rotation.y = 0.9 + i * 1.7;                    // labels facing different ways
    set.scene.add(s);
  });
  // a long workbench: the desk runs out of frame instead of ending at the room
  const desk = set.scene.getObjectByName("Desk");
  desk.castShadow = false;
  desk.scale.set(2.4, 1, 2);
  for (const t of ["map", "normalMap", "roughnessMap", "metalnessMap"]) {
    const tex = desk.material[t];
    if (!tex) continue;
    tex.repeat.x *= 2.4; tex.repeat.y *= 2;
    tex.anisotropy = anisotropy;
  }
  // the bench is dressed for the film camera; for the web's wider shots push the station back
  set.scene.traverse((o) => { if (/^(Station|IronHolder|Iron)/.test(o.name)) { o.position.x += 0.42; o.position.z -= 0.34; } });
  set.scene.traverse((o) => { if (/^(SolderSpool|WireSpool)/.test(o.name)) { o.position.x -= 0.15; o.position.z -= 0.05; } });   // off the mat, onto the bench
  const mats = restyle(lynx.scene);
  scene.add(set.scene, lynx.scene);
  const node = (n) => lynx.scene.getObjectByName(n);
  const pan = node("PanPivot"), tilt = node("TiltPivot");
  const holders = {};
  lynx.scene.traverse((o) => { if (o.name.endsWith("_Move")) holders[o.name.replace(/_Move$/, "")] = o; });
  const restPos = Object.fromEntries(Object.entries(holders).map(([k, h]) => [k, h.position.clone()]));
  const led = mats.get("led"), lightPipe = mats.get("PETG_clear");

  // offsets are authored in Blender part-local mm, like the video storyboard
  const EXPL = {
    Lid: [0, 0, 46], Jig_XIAO_Sense: [0, 0, 30], Jig_MicroSD: [34, 0, 30], Jig_Heatsink: [0, 0, 15],
    Jig_Antenna_Coax: [-16, 0, 30], Jig_FPC_Antenna: [-30, 0, 8], BatteryTray: [0, 40, 8], Jig_LiPo_401723: [0, 40, -2],
    Jig_USB_Plug_RA: [26, 0, 6], Jig_Horn_Tilt: [14, 0, -4], Jig_Servo_Tilt: [26, 0, 0], Jig_Horn_Pan: [0, 0, -9],
    Jig_Servo_Pan: [0, 0, -24], PanBase: [0, 0, -40], Jig_Magnet: [0, 0, -58], Jig_USB_PanelSocket: [0, 22, -40],
    PanCap: [0, 0, -14], ServoShroud: [46, 0, 0], LightPipePrint: [0, 0, 38], Jig_SpringReliefs: [0, 26, -30],
  };
  // explode amount per part (0 assembled .. 1 exploded); the tilt group lifts off the yoke as a whole
  const explode = Object.fromEntries(Object.keys(EXPL).map((k) => [k, 0]));
  const state = { pan: 0, tilt: REST_TILT, lift: 0, plug: 1, card: 1, led: 0, arcs: 0 };
  const LIFT_MM = 22;

  function setLed(amount) {
    // the LED and its light pipe glow (and bloom); no light is cast: from inside the shell, with no
    // shadow, a point light only tinted the parts around the head green
    led.emissiveIntensity = amount * 6;
    lightPipe.emissiveIntensity = amount * 1.2;
  }

  function applyParts() {
    for (const [k, amt] of Object.entries(explode)) {
      const h = holders[k];
      if (!h) continue;
      const [x, y, z] = EXPL[k];
      h.position.copy(restPos[k]).add(b2l(x * amt, y * amt, z * amt));
    }
    // card and plug have their own insertion paths
    const card = holders.Jig_MicroSD;
    if (card && explode.Jig_MicroSD === 0) card.position.copy(restPos.Jig_MicroSD).add(b2l(22 * state.card, 0, 0));
    const plug = holders.Jig_USBC_PowerPlug;
    if (plug) plug.position.copy(restPos.Jig_USBC_PowerPlug).add(b2l(0, 26 * state.plug, 0));
    tilt.position.y = rig.pivots.tz + LIFT_MM * state.lift;
    pan.rotation.y = THREE.MathUtils.degToRad(state.pan);
    tilt.rotation.x = THREE.MathUtils.degToRad(state.tilt);
    setLed(state.led);
  }

  // ------------------------------------------------------------------ cables (drawn from the CAD waypoints)
  lynx.scene.updateMatrixWorld(true);
  // printed marks, placed on the model at rest (head upright): PAN/TILT, the eye mark and REV D on the
  // pan base's front panel; the wordmark on the head's front, over the small embossed one
  printOn(holders.PanBase, node("PanBase"), new THREE.Vector3(0.0205, -0.049, 0.2), 30, 12,
    new THREE.MeshStandardMaterial({ map: printMap(30, 12, basePrint, anisotropy), transparent: true, roughness: 0.6,
      depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 }));
  const shell = mats.get("PLA_offwhite").clone();
  shell.color.set(0xffffff);                         // the print carries the shell's colour
  shell.map = printMap(8, 3.2, headPrint, anisotropy);
  printOn(holders.Base, node("Base"), new THREE.Vector3(0.0225, 0.003, 0.2), 8, 3.2, shell, 0.0003);

  const groupNode = { tilt, pan, fixed: node("Root") };
  function anchorPoints(list) {
    // waypoint -> its rig group's local frame, captured at rest so it follows that group afterwards
    return list.map(([p, g]) => {
      const n = groupNode[g] || groupNode.fixed;
      return { n, local: n.worldToLocal(b2t(...p)) };
    });
  }
  const internalAnchors = anchorPoints(rig.cable_internal);
  const plugNode = holders.Jig_USBC_PowerPlug;
  // the outside cable's first two waypoints ride on the plug; then it lies on the mat, in a loose loop
  // around the base (clear of the chips) before it leaves the bench to the back left. Blender mm.
  const floorZ = rig.cable_outside.at(-1)[2];
  const LOOP = [[-38, 72], [-42, 30], [-32, -12], [-5, -32], [30, -36], [60, -22], [72, 10], [66, 48], [45, 80],
    [10, 108], [-50, 122], [-130, 148], [-250, 190]];
  const extAnchors = rig.cable_outside.concat(LOOP.map(([x, y]) => [x, y, floorZ]))
    .map((p, i) => { const n = i < 2 ? plugNode : groupNode.fixed; return { n, local: n.worldToLocal(b2t(...p)) }; });
  function cableMesh(anchors, radius, material, segments) {
    const mesh = new THREE.Mesh(new THREE.BufferGeometry(), material);
    mesh.castShadow = true;
    mesh.userData = { anchors, radius, segments };
    scene.add(mesh);
    return mesh;
  }
  // both braided; the bump's repeat follows each cable's length so the weave is the same size on both
  const cables = [cableMesh(internalAnchors, 0.002, cableMaterial(80), 160), cableMesh(extAnchors, 0.0021, cableMaterial(400), 420)];
  const tmp = new THREE.Vector3();
  // cables: one TubeGeometry per cable, rewritten in place (no per-frame allocation or GPU re-create),
  // and only when an anchor actually moved
  const TUBE_RAD = 12;
  const _n = new THREE.Vector3(), _v = new THREE.Vector3();
  function updateCables() {
    for (const c of cables) {
      const u = c.userData;
      u.pts ??= u.anchors.map(() => new THREE.Vector3());
      let moved = !u.built;
      u.anchors.forEach((a, i) => {
        a.n.localToWorld(_v.copy(a.local));
        if (_v.distanceToSquared(u.pts[i]) > 1e-12) { u.pts[i].copy(_v); moved = true; }
      });
      if (!moved) continue;
      u.curve ??= new THREE.CatmullRomCurve3(u.pts, false, "centripetal");
      if (!u.built) {
        c.geometry.dispose();
        c.geometry = new THREE.TubeGeometry(u.curve, u.segments, u.radius, TUBE_RAD);
        c.geometry.attributes.position.setUsage(THREE.DynamicDrawUsage);
        c.geometry.attributes.normal.setUsage(THREE.DynamicDrawUsage);
        u.built = true;
        continue;
      }
      const fr = u.curve.computeFrenetFrames(u.segments, false);
      const pos = c.geometry.attributes.position.array, nor = c.geometry.attributes.normal.array;
      let k = 0;
      for (let i = 0; i <= u.segments; i++) {
        u.curve.getPointAt(i / u.segments, _v);
        const N = fr.normals[i], B = fr.binormals[i];
        for (let j = 0; j <= TUBE_RAD; j++) {
          const a = (j / TUBE_RAD) * Math.PI * 2, sn = Math.sin(a), cs = -Math.cos(a);
          _n.set(cs * N.x + sn * B.x, cs * N.y + sn * B.y, cs * N.z + sn * B.z).normalize();
          nor[k] = _n.x; nor[k + 1] = _n.y; nor[k + 2] = _n.z;
          pos[k] = _v.x + u.radius * _n.x; pos[k + 1] = _v.y + u.radius * _n.y; pos[k + 2] = _v.z + u.radius * _n.z;
          k += 3;
        }
      }
      c.geometry.attributes.position.needsUpdate = true;
      c.geometry.attributes.normal.needsUpdate = true;
      c.geometry.computeBoundingSphere();
    }
  }

  // range-of-motion arcs (thin glowing rings around the pivots)
  function arc(radius, from, to, color) {
    const curve = new THREE.EllipseCurve(0, 0, radius, radius, from, to);
    const g = new THREE.BufferGeometry().setFromPoints(curve.getPoints(96).map((p) => new THREE.Vector3(p.x, 0, p.y)));
    // writes depth so depth of field keeps it sharp at the pivot; hidden while faded out (see setArcs)
    return new THREE.Line(g, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0 }));
  }
  const panArc = arc(0.05, -Math.PI / 2 - Math.PI / 2, -Math.PI / 2 + Math.PI / 2, 0x5ff0a1);
  panArc.position.copy(b2t(rig.pivots.pan_x, rig.pivots.cy, rig.pivots.floor_z + 26));
  scene.add(panArc);
  const tiltArc = arc(0.045, -Math.PI / 3, Math.PI / 3, 0xf0c48a);
  tiltArc.rotation.z = Math.PI / 2;
  tiltArc.position.copy(b2t(rig.pivots.pan_x - 22, rig.pivots.cy, rig.pivots.tz));
  scene.add(tiltArc);
  function setArcs(amount) {
    for (const a of [panArc, tiltArc]) {
      a.visible = amount > 0;
      a.material.opacity = amount * 0.8;
    }
  }
  setArcs(0);

  // ------------------------------------------------------------------ post
  // Desktop: N8AOPass is the scene render (it draws the beauty pass and its depth itself), and depth of
  // field reads that same depth texture, so the scene is drawn once per frame. Hardware MSAA doesn't
  // survive the AO pass, so SMAA runs last, on the display-referred image.
  // Phones: one MSAA scene render, bloom for the LED, output. No AO, depth of field or SMAA.
  const composer = new EffectComposer(renderer);
  const { x: w, y: h } = renderer.getDrawingBufferSize(new THREE.Vector2());
  const ao = new N8AOPass(scene, camera, w, h);
  Object.assign(ao.configuration, {
    aoRadius: 0.02, distanceFalloff: 0.01, intensity: 2.2, halfRes: true,
    gammaCorrection: false,                          // OutputPass does the colour conversion once, at the end
  });
  // nothing here needs transparency-aware AO (the only transparent things are thin lines); auto-detect
  // would turn it on and draw the scene twice more every frame
  ao.autoDetectTransparency = false;
  // shallow depth of field like the renders: main.js and build.js keep the focus on the product
  const dof = new DepthOfFieldPass(camera, ao.beautyRenderTarget.depthTexture, { focus: 0.34, aperture: 0.02, maxblur: 0.012 });
  if (TOUCH) {
    composer.renderTarget1.samples = composer.renderTarget2.samples = 4;
    composer.addPass(new RenderPass(scene, camera));
  } else {
    composer.addPass(ao);
    composer.addPass(dof);
  }
  composer.addPass(new UnrealBloomPass(new THREE.Vector2(w, h), 0.25, 0.4, 0.92));
  composer.addPass(new OutputPass());
  if (!TOUCH) composer.addPass(new SMAAPass(w, h));

  // ------------------------------------------------------------------ size
  // The drawing buffer follows the canvas's CSS box (100vw x 100lvh), observed directly. Measuring it
  // once at start-up could catch the canvas before the stylesheet applied (the default 300 x 150,
  // stretched and blurry until the window was resized), and a window resize misses zoom, a move to a
  // screen of another density and a page restored from the back/forward cache.
  const STEPS = [1, 0.85, 0.7];                      // adaptive-resolution scales, see govern()
  let step = 0;
  const resizeListeners = new Set();
  function fit() {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    const ratio = Math.max(0.75, Math.min(devicePixelRatio, MAX_RATIO) * STEPS[step]);
    renderer.setPixelRatio(ratio);
    renderer.setSize(w, h, false);
    composer.setPixelRatio(ratio);
    composer.setSize(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    for (const listener of resizeListeners) listener(w, h);
  }
  const onResize = (listener) => resizeListeners.add(listener);
  new ResizeObserver(fit).observe(canvas);
  (function watchDensity() {
    matchMedia(`(resolution: ${devicePixelRatio}dppx)`).addEventListener("change", () => { fit(); watchDensity(); }, { once: true });
  })();
  addEventListener("pageshow", (e) => { if (e.persisted) fit(); });
  fit();

  // Adaptive resolution. Frames drawn back to back are timed once the page has settled (its first
  // seconds compile shaders and upload textures, which would read as a slow GPU). Below ~35 fps for two
  // seconds the resolution steps down; at a steady 50+ fps it steps back up, waiting twice as long after
  // every step down so it never see-saws.
  let settledAt = Infinity, lastFrame = -Infinity, slowFor = 0, fastFor = 0, upAfter = 8000;
  function govern(time) {
    if (settledAt === Infinity) settledAt = time + 4000;   // four seconds from the first frame
    const dt = time - lastFrame;
    lastFrame = time;
    if (time < settledAt || dt > 100) return;        // warming up, or the first frame after idling
    if (dt > 28) { slowFor += dt; fastFor = 0; } else { slowFor = Math.max(0, slowFor - dt); fastFor = dt < 20 ? fastFor + dt : 0; }
    if (slowFor > 2000 && step < STEPS.length - 1) { step++; upAfter *= 2; slowFor = fastFor = 0; fit(); }
    else if (fastFor > upAfter && step > 0) { step--; slowFor = fastFor = 0; fit(); }
  }

  // The key light and the bench never move, so the shadow map is only redrawn when the product does
  // (pan, tilt, lift, a part flying in) or a page says parts were shown or hidden (invalidateShadows).
  renderer.shadowMap.autoUpdate = false;
  const holderList = Object.values(holders);
  const partsNow = new Float64Array(3 + holderList.length * 3), partsShadowed = new Float64Array(partsNow.length).fill(NaN);
  function invalidateShadows() { partsShadowed.fill(NaN); }
  function partsMoved() {
    partsNow[0] = pan.rotation.y; partsNow[1] = tilt.rotation.x; partsNow[2] = tilt.position.y;
    holderList.forEach((h, i) => h.position.toArray(partsNow, 3 + i * 3));
    const moved = partsNow.some((v, i) => v !== partsShadowed[i]);   // NaN (invalidated) counts as moved
    if (moved) partsShadowed.set(partsNow);
    return moved;
  }

  function render(time = performance.now()) {
    govern(time);
    if (partsMoved()) renderer.shadowMap.needsUpdate = true;
    composer.render();
  }

  return {
    renderer, scene, camera, key, lynx, set, rig, mats, node, pan, tilt, holders, restPos, setLed,
    EXPL, explode, state, LIFT_MM, applyParts, groupNode, cables, updateCables, tmp, setArcs,
    composer, render, onResize, invalidateShadows, ao, dof,
  };
}

// A print laid flat on a part: a ray from the front finds the face, the w x h mm print sits on it, lifted
// clear of the surface, and rides with the part's holder (explode, pan and tilt included).
function printOn(holder, part, from, w, h, material, lift = 0.00005, toward = new THREE.Vector3(0, 0, -1), twist = 0) {
  const hit = new THREE.Raycaster(from, toward).intersectObject(part, true)[0];
  const normal = hit.face.normal.clone().transformDirection(hit.object.matrixWorld);
  const print = new THREE.Mesh(new THREE.PlaneGeometry(w * MM, h * MM), material);
  print.position.copy(hit.point).addScaledVector(normal, lift);
  print.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), normal);
  print.rotateZ(twist);
  holder.attach(print);
}

// a print's artwork: drawn at 24 px/mm (in millimetres) with the page's fonts
function printMap(w, h, draw, anisotropy) {
  const PX = 24, c = document.createElement("canvas");
  c.width = w * PX; c.height = h * PX;
  const g = c.getContext("2d");
  g.scale(PX, PX);
  draw(g, w, h);
  const map = new THREE.CanvasTexture(c);
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = anisotropy;
  return map;
}

// light grey on the gunmetal, like the finished-hardware renders: PAN/TILT, the eye mark, REV D
function basePrint(g, w, h) {
  g.fillStyle = g.strokeStyle = "rgba(214,218,224,0.86)";
  g.textBaseline = "middle";
  g.font = '600 2px "Martian Mono", ui-monospace, monospace';
  g.textAlign = "left"; g.fillText("PAN/TILT", 1.2, h / 2);
  g.textAlign = "right"; g.fillText("REV D", w - 1.2, h / 2);
  g.lineWidth = 0.45;
  g.beginPath(); g.arc(w / 2, h / 2, 2.1, 0, Math.PI * 2); g.stroke();
  g.beginPath(); g.arc(w / 2, h / 2, 0.8, 0, Math.PI * 2); g.fill();
}

// the wordmark in mid grey on the shell's white (the background is the shell's own colour)
function headPrint(g, w, h) {
  g.fillStyle = "#e8e9e8"; g.fillRect(0, 0, w, h);
  g.fillStyle = "#8b9097";
  g.font = "800 2.4px Archivo, system-ui, sans-serif";
  g.fontStretch = "expanded";
  g.textAlign = "right"; g.textBaseline = "middle";
  g.fillText("lynx", w - 0.4, h / 2);
}
