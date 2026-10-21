// SPDX-License-Identifier: MIT
// lynx landing page: one immersive scene, driven by scroll.
//   hero: the camera self-assembles and looks at your cursor
//   usb: it turns, the USB-C goes into the base, the LED wakes
//   sd: the microSD slides in
//   motion: pan/tilt sweep its full range, with range arcs
//   assembly: scroll explodes it and puts it back together, step by step
//   cta: back to the hero, with the calls to action
import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { DRACOLoader } from "three/examples/jsm/loaders/DRACOLoader.js";
import { RGBELoader } from "three/examples/jsm/loaders/RGBELoader.js";
import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/examples/jsm/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
import { BokehPass } from "three/examples/jsm/postprocessing/BokehPass.js";
import { N8AOPass } from "n8ao";
import gsap from "gsap";
import { ScrollTrigger } from "gsap/ScrollTrigger";
import { restyle, cableMaterial, powerCableMaterial } from "./materials.js";

gsap.registerPlugin(ScrollTrigger);
const MM = 0.001;
const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;
// Blender (z-up, mm) -> three (y-up, m)
const b2t = (x, y, z) => new THREE.Vector3(x * MM, z * MM, -y * MM);      // world, metres
const b2l = (x, y, z) => new THREE.Vector3(x, z, -y);                       // under Root: millimetres

// ------------------------------------------------------------------ renderer + scene
const canvas = document.getElementById("scene");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: "high-performance" });
renderer.setPixelRatio(Math.min(devicePixelRatio, 1.5));
renderer.setSize(innerWidth, innerHeight);
renderer.toneMapping = THREE.AgXToneMapping;
renderer.toneMappingExposure = 0.82;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b0b0d);
scene.fog = new THREE.Fog(0x0b0b0d, 0.7, 1.9);
const camera = new THREE.PerspectiveCamera(30, innerWidth / innerHeight, 0.005, 5);

// lights: warm desk lamp (key, shadows), cool window fill, rim
const key = new THREE.DirectionalLight(0xffd2a0, 2.6);
key.position.set(-0.24, 0.28, 0.16);
key.castShadow = true;
key.shadow.mapSize.set(2048, 2048);
Object.assign(key.shadow.camera, { left: -0.15, right: 0.15, top: 0.15, bottom: -0.15, near: 0.01, far: 1 });
key.shadow.bias = -0.0004;
key.shadow.radius = 4;
scene.add(key);
scene.add(new THREE.HemisphereLight(0xbfd4ff, 0x1a1208, 0.10));
const rim = new THREE.DirectionalLight(0xfff0dc, 1.6);
rim.position.set(0.08, 0.26, -0.34);
scene.add(rim);
const ledLight = new THREE.PointLight(0x40ff88, 0, 0.06, 2);
scene.add(ledLight);

// ------------------------------------------------------------------ loading
const bar = document.querySelector("#loader .bar span");
const manager = new THREE.LoadingManager();
manager.onProgress = (_, done, total) => (bar.style.width = `${(100 * done) / total}%`);
const draco = new DRACOLoader(manager).setDecoderPath("./draco/");
const gltf = new GLTFLoader(manager).setDRACOLoader(draco);
const [lynx, set, rig, env] = await Promise.all([
  gltf.loadAsync("./assets/lynx.glb"),
  gltf.loadAsync("./assets/set.glb"),
  fetch("./assets/rig.json").then((r) => r.json()),
  new RGBELoader(manager).loadAsync("./assets/studio_1k.hdr"),
]);
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromEquirectangular(env).texture;
scene.environmentIntensity = 0.32;
env.dispose();

restyle(set.scene);
// the bench is dressed for the film camera; for the web's wider shots push the station back
set.scene.traverse((o) => { if (/^(Station|IronHolder|Iron)/.test(o.name)) { o.position.x += 0.42; o.position.z -= 0.34; } });
set.scene.traverse((o) => { if (/^(SolderSpool|WireSpool)/.test(o.name)) { o.position.x -= 0.07; o.position.z -= 0.05; } });   // clear the close-ups
const mats = restyle(lynx.scene);
scene.add(set.scene, lynx.scene);
const node = (n) => lynx.scene.getObjectByName(n);
const pan = node("PanPivot"), tilt = node("TiltPivot");
const holders = {};
lynx.scene.traverse((o) => { if (o.name.endsWith("_Move")) holders[o.name.replace(/_Move$/, "")] = o; });
const restPos = Object.fromEntries(Object.entries(holders).map(([k, h]) => [k, h.position.clone()]));
const led = mats.get("led");

// offsets are authored in Blender part-local mm, like the video storyboard
const EXPL = {
  Lid: [0, 0, 46], Jig_XIAO_Sense: [0, 0, 30], Jig_MicroSD: [34, 0, 30], Jig_Heatsink: [0, 0, 15],
  Jig_Antenna_Coax: [-16, 0, 30], Jig_FPC_Antenna: [-30, 0, 8], BatteryTray: [0, 40, 8], Jig_LiPo_401723: [0, 40, -2],
  Jig_USB_Plug_RA: [26, 0, 6], Jig_Horn_Tilt: [14, 0, -4], Jig_Servo_Tilt: [26, 0, 0], Jig_Horn_Pan: [0, 0, -9],
  Jig_Servo_Pan: [0, 0, -24], PanBase: [0, 0, -40], Jig_Magnet: [0, 0, -58], Jig_USB_PanelSocket: [0, 22, -40],
  PanCap: [0, 0, -14], ServoShroud: [46, 0, 0], LightPipePrint: [0, 0, 38], Jig_SpringReliefs: [0, 26, -30],
};
const ASSEMBLY = [
  ["Jig_Magnet", "Magnet into the base"], ["Jig_Servo_Pan", "Pan servo, two screws"], ["PanCap", "Pan cap, two side screws"],
  ["Jig_Horn_Pan", "Yoke onto the pan horn"], ["Jig_Servo_Tilt", "Tilt servo into the yoke"], ["ServoShroud", "Finned shroud over it"],
  ["Jig_LiPo_401723", "Battery"], ["BatteryTray", "Tray clicks in"], ["Jig_XIAO_Sense", "Board onto the tray"],
  ["Lid", "Light pipe, lid, two screws"],
];
document.getElementById("steps").innerHTML = ASSEMBLY.map(([, t]) => `<li>${t}</li>`).join("");
// explode amount per part (0 assembled .. 1 exploded); the tilt group lifts off the yoke as a whole
const explode = Object.fromEntries(Object.keys(EXPL).map((k) => [k, 0]));
const REST_TILT = 45;   // at rest the lens leans 45 deg toward the front (0 = straight up)
const state = { pan: 0, tilt: REST_TILT, lift: 0, plug: 1, card: 1, led: 0, arcs: 0 };
const LIFT_MM = 22;

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
  led.emissiveIntensity = state.led * 6;
  ledLight.intensity = state.led * 0.02;
}

// ------------------------------------------------------------------ cables (drawn from the CAD waypoints)
lynx.scene.updateMatrixWorld(true);
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
// the outside cable's first two waypoints ride on the plug; the rest lie on the desk
const extAnchors = rig.cable_outside.concat([[rig.cable_outside.at(-1)[0] - 80, rig.cable_outside.at(-1)[1] + 40, rig.cable_outside.at(-1)[2]],
  [rig.cable_outside.at(-1)[0] - 200, rig.cable_outside.at(-1)[1] + 90, rig.cable_outside.at(-1)[2]]])
  .map((p, i) => { const n = i < 2 ? plugNode : groupNode.fixed; return { n, local: n.worldToLocal(b2t(...p)) }; });
function cableMesh(anchors, radius, material) {
  const mesh = new THREE.Mesh(new THREE.BufferGeometry(), material);
  mesh.castShadow = true;
  mesh.userData = { anchors, radius };
  scene.add(mesh);
  return mesh;
}
const cables = [cableMesh(internalAnchors, 0.002, cableMaterial()), cableMesh(extAnchors, 0.0019, powerCableMaterial())];
const tmp = new THREE.Vector3();
// cables: one TubeGeometry per cable, rewritten in place (no per-frame allocation or GPU re-create),
// and only when an anchor actually moved
const TUBE_SEG = 160, TUBE_RAD = 12;
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
      c.geometry = new THREE.TubeGeometry(u.curve, TUBE_SEG, u.radius, TUBE_RAD);
      c.geometry.attributes.position.setUsage(THREE.DynamicDrawUsage);
      c.geometry.attributes.normal.setUsage(THREE.DynamicDrawUsage);
      u.built = true;
      continue;
    }
    const fr = u.curve.computeFrenetFrames(TUBE_SEG, false);
    const pos = c.geometry.attributes.position.array, nor = c.geometry.attributes.normal.array;
    let k = 0;
    for (let i = 0; i <= TUBE_SEG; i++) {
      u.curve.getPointAt(i / TUBE_SEG, _v);
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
  return new THREE.Line(g, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0, depthWrite: false }));
}
const panArc = arc(0.05, -Math.PI / 2 - Math.PI / 2, -Math.PI / 2 + Math.PI / 2, 0x7fd6a4);
panArc.position.copy(b2t(rig.pivots.pan_x, rig.pivots.cy, rig.pivots.floor_z + 26));
scene.add(panArc);
const tiltArc = arc(0.045, -Math.PI / 3, Math.PI / 3, 0xf0c48a);
tiltArc.rotation.z = Math.PI / 2;
tiltArc.position.copy(b2t(rig.pivots.pan_x - 22, rig.pivots.cy, rig.pivots.tz));
scene.add(tiltArc);

// ------------------------------------------------------------------ post
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const ao = new N8AOPass(scene, camera, innerWidth, innerHeight);
ao.configuration.aoRadius = 0.02;
ao.configuration.distanceFalloff = 0.01;
ao.configuration.intensity = 2.2;
ao.configuration.halfRes = true;
ao.configuration.gammaCorrection = false;        // OutputPass does the colour conversion once, at the end
composer.addPass(ao);
composer.addPass(new UnrealBloomPass(new THREE.Vector2(innerWidth, innerHeight), 0.25, 0.4, 0.92));
// shallow depth of field like the renders: focus tracks the product every frame
const bokeh = new BokehPass(scene, camera, { focus: 0.34, aperture: 0.0035, maxblur: 0.006 });
composer.addPass(bokeh);
composer.addPass(new OutputPass());

// ------------------------------------------------------------------ camera shots (Blender convention)
const C = rig.pivots;
const HERO = [C.pan_x, C.cy, -12];
function shotPose([tx, ty, tz], az, el, dist, fov) {
  const a = THREE.MathUtils.degToRad(az), e = THREE.MathUtils.degToRad(el), d = dist * 1000;
  const t = b2t(tx, ty, tz);
  const p = b2t(tx + d * Math.cos(e) * Math.sin(a), ty - d * Math.cos(e) * Math.cos(a), tz + d * Math.sin(e));
  return { target: t, pos: p, fov };
}
const SHOTS = {
  hero: shotPose(HERO, -32, 16, 0.40, 28),
  usb: shotPose([C.pan_x, rig.pod_face_y - 4, C.floor_z + 16], 150, 20, 0.30, 30),
  sd: shotPose([rig.usb_axis[0] - 3, rig.usb_axis[1], rig.usb_axis[2] + 4], 38, 34, 0.22, 30),
  motion: shotPose([C.pan_x, C.cy, -6], 12, 10, 0.30, 30),
  assembly: shotPose([C.pan_x, C.cy, 0], -18, 16, 0.52, 32),
  cta: shotPose([C.pan_x, C.cy, -34], -40, 12, 0.44, 28),        // target low: product sits high, CTAs below
};
const cam = { pos: SHOTS.hero.pos.clone(), target: SHOTS.hero.target.clone(), fov: 30 };
const smooth = { pos: cam.pos.clone(), target: cam.target.clone(), fov: 30 };

// ------------------------------------------------------------------ timeline
function toShot(tl, name, at, dur = 1) {
  const s = SHOTS[name];
  tl.to(cam.pos, { x: s.pos.x, y: s.pos.y, z: s.pos.z, duration: dur, ease: "power2.inOut" }, at);
  tl.to(cam.target, { x: s.target.x, y: s.target.y, z: s.target.z, duration: dur, ease: "power2.inOut" }, at);
  tl.to(cam, { fov: s.fov, duration: dur, ease: "power2.inOut" }, at);
}
const beats = [...document.querySelectorAll(".beat")];
let beatAt = [];
const measureBeats = () => { const H = document.documentElement.scrollHeight - innerHeight; beatAt = beats.map((b) => b.offsetTop / H); };
measureBeats();
const at = (i) => beatAt[i];   // cached: no layout reads inside the frame loop

function buildTimeline() {
  const tl = gsap.timeline({ paused: true, defaults: { ease: "power2.inOut" } });
  // usb
  toShot(tl, "usb", at(1) - 0.06, 0.08);
  tl.to(state, { pan: -25, duration: 0.06 }, at(1) - 0.06);
  tl.to(state, { plug: 0, duration: 0.05 }, at(1) + 0.02);
  tl.to(state, { led: 1, duration: 0.01 }, at(1) + 0.075);
  // sd
  toShot(tl, "sd", at(2) - 0.08, 0.11);                    // slower, gentler move into the close-up
  tl.to(state, { pan: 0, tilt: REST_TILT, duration: 0.08 }, at(2) - 0.08);
  tl.to(state, { card: 0, duration: 0.05 }, at(2) + 0.02);
  // motion: sweep with eased glances
  toShot(tl, "motion", at(3) - 0.05, 0.07);
  tl.to(state, { arcs: 1, duration: 0.03 }, at(3));
  const m0 = at(3) + 0.02, m1 = at(4) - 0.06, step = (m1 - m0) / 6;
  [[-88, REST_TILT], [88, REST_TILT], [0, -58], [0, 58], [-40, 25], [0, REST_TILT]].forEach(([p, t], i) =>
    tl.to(state, { pan: p, tilt: t, duration: step * 0.9, ease: "back.inOut(1.2)" }, m0 + i * step));
  tl.to(state, { arcs: 0, duration: 0.03 }, m1);
  // assembly: explode, then put back in build order
  toShot(tl, "assembly", at(4) - 0.04, 0.06);
  const a0 = at(4) + 0.01;
  tl.to(state, { lift: 1, duration: 0.03 }, a0);
  Object.keys(EXPL).forEach((k, i) => tl.to(explode, { [k]: 1, duration: 0.03 }, a0 + i * 0.002));
  const b0 = a0 + 0.06, b1 = at(5) - 0.05, bs = (b1 - b0) / ASSEMBLY.length;
  ASSEMBLY.forEach(([k], i) => {
    tl.to(explode, { [k]: 0, duration: bs * 0.8, ease: "power3.out" }, b0 + i * bs);
    tl.call(() => document.querySelectorAll("#steps li").forEach((li, j) => { li.classList.toggle("on", j <= i); li.classList.toggle("now", j === i); }),
      null, b0 + i * bs);
  });
  const rest = Object.keys(EXPL).filter((k) => !ASSEMBLY.find(([a]) => a === k));
  rest.forEach((k) => tl.to(explode, { [k]: 0, duration: bs }, b1 - bs));
  tl.to(state, { lift: 0, duration: bs }, b1 - bs * 0.5);
  // cta
  toShot(tl, "cta", at(5) - 0.04, 0.06);
  tl.set({}, {}, 1);
  return tl;
}

// intro: assemble into view on load (not scroll-driven)
function intro() {
  Object.keys(EXPL).forEach((k) => (explode[k] = 1));
  state.lift = 1;
  const t = gsap.timeline({ delay: 0.3 });
  const order = ASSEMBLY.map(([k]) => k).concat(Object.keys(EXPL).filter((k) => !ASSEMBLY.find(([a]) => a === k)));
  order.forEach((k, i) => t.to(explode, { [k]: 0, duration: 1.0, ease: "power3.out" }, i * 0.07));
  t.to(state, { lift: 0, duration: 0.9, ease: "power3.inOut" }, 0.55);
  const start = shotPose(HERO, -70, 30, 0.6, 34);
  cam.pos.copy(start.pos); smooth.pos.copy(start.pos);
  t.to(cam.pos, { x: SHOTS.hero.pos.x, y: SHOTS.hero.pos.y, z: SHOTS.hero.pos.z, duration: 2.4, ease: "power3.inOut" }, 0);
  return t;
}

// ------------------------------------------------------------------ interaction: cursor look + CTA tilt
const mouse = new THREE.Vector2();
// 3D tilt + specular sheen on every glass slab and keycap, toward the cursor
const tiltables = [...document.querySelectorAll(".slab, .key")];
addEventListener("pointermove", (e) => {
  mouse.set((e.clientX / innerWidth) * 2 - 1, (e.clientY / innerHeight) * 2 - 1);
  for (const el of tiltables) {
    const r = el.getBoundingClientRect();
    if (r.bottom < 0 || r.top > innerHeight) continue;
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    const dx = THREE.MathUtils.clamp((e.clientX - cx) / (r.width * 1.6), -1, 1);
    const dy = THREE.MathUtils.clamp((e.clientY - cy) / (r.height * 1.6), -1, 1);
    const strength = el.classList.contains("key") ? 10 : 6;
    const tgt = el.classList.contains("key") ? el : el.querySelector(".face");
    tgt.style.setProperty("--ry", `${dx * strength}deg`);
    tgt.style.setProperty("--rx", `${-dy * strength}deg`);
    tgt.style.setProperty("--mx", `${((e.clientX - r.left) / r.width) * 100}%`);
    tgt.style.setProperty("--my", `${((e.clientY - r.top) / r.height) * 100}%`);
  }
});
const reveal = new IntersectionObserver((es) => es.forEach((e) => e.target.classList.toggle("in", e.isIntersecting)), { threshold: 0.3 });
document.querySelectorAll(".slab").forEach((el) => reveal.observe(el));

// chapter rail: click to jump, highlight the current chapter
const railLinks = [...document.querySelectorAll("#rail a")];
railLinks.forEach((a) => a.addEventListener("click", () => beats[+a.dataset.to].scrollIntoView({ behavior: "smooth" })));
const railFill = document.querySelector("#rail .fill");

// callouts: each anchored beat's slab points at a spot on the product (rides on that part's rig group)
const ANCHORS = {
  pod: { n: groupNode.fixed, p: [C.pan_x + 5.35, rig.pod_face_y, C.floor_z + 13] },
  sd: { n: tilt, p: [rig.usb_axis[0] + 0.5, (rig.sd_card.min[1] + rig.sd_card.max[1]) / 2, (rig.sd_card.min[2] + rig.sd_card.max[2]) / 2] },
  pivot: { n: tilt, p: [rig.usb_axis[0] + 1.3, C.cy, C.tz] },
};
for (const a of Object.values(ANCHORS)) a.local = a.n.worldToLocal(b2t(...a.p));
const callout = document.getElementById("callout"), leader = document.getElementById("leader");
const target = document.getElementById("target"), targetDot = document.getElementById("target-dot");
const roPan = document.getElementById("ro-pan"), roTilt = document.getElementById("ro-tilt");
const fmt = (v) => `${v >= 0 ? "+" : "−"}${Math.abs(v).toFixed(1).padStart(4, "0")}°`;
function updateReadout(livePan, liveTilt) {   // text writes run after the layout read in updateChrome
  const p = fmt(livePan), t = fmt(liveTilt);
  if (roPan.textContent !== p) roPan.textContent = p;
  if (roTilt.textContent !== t) roTilt.textContent = t;
}
function updateChrome() {
  railFill.style.transform = `scaleX(${progress})`;
  let cur = 0;
  const y = (goal * (document.documentElement.scrollHeight - innerHeight) + innerHeight * 0.45) / (document.documentElement.scrollHeight - innerHeight);
  beatAt.forEach((t, i) => { if (t <= y) cur = i; });
  railLinks.forEach((a) => a.classList.toggle("on", +a.dataset.to === cur || (cur === 5 && +a.dataset.to === 4)));
  // leader line from the visible slab to its anchor
  const beat = beats[cur], key = beat.dataset.anchor, slab = beat.querySelector(".slab");
  if (!key || !slab || !slab.classList.contains("in") || (introTl && introTl.isActive())) { callout.classList.add("hide"); return; }
  const w = ANCHORS[key].n.localToWorld(tmp.copy(ANCHORS[key].local)).project(camera);
  const tx = (w.x * 0.5 + 0.5) * innerWidth, ty = (-w.y * 0.5 + 0.5) * innerHeight;
  const r = slab.querySelector(".face").getBoundingClientRect();
  const fromRight = slab.classList.contains("left");
  const sx = fromRight ? r.right : r.left, sy = Math.min(Math.max(ty, r.top + 28), r.bottom - 28);
  const kx = sx + (fromRight ? 1 : -1) * Math.min(60, Math.abs(tx - sx) * 0.4);
  leader.setAttribute("d", `M${sx},${sy} L${kx},${sy} L${tx},${ty}`);
  for (const c of [target, targetDot]) { c.setAttribute("cx", tx); c.setAttribute("cy", ty); }
  callout.classList.toggle("hide", w.z > 1 || Math.abs(tx - sx) < 40);
}

// ------------------------------------------------------------------ run
let tl = buildTimeline();
// scroll sets a goal; the scene eases toward it every frame, so wheel steps glide instead of jump
let goal = 0, progress = 0, vel = 0;
const TOUCH = matchMedia("(pointer: coarse)").matches;
const MAX_RATE = TOUCH ? 0.16 : 0.3;   // timeline progress per second: caps how fast a fling can drive the scene
ScrollTrigger.create({ start: 0, end: "max", onUpdate: (st) => (goal = st.progress) });
// The timeline records each tween's start values when it is built, so it must be rebuilt from the
// rest pose: rewind to 0 first, or the pose at that moment (half exploded, mid-pan) becomes the
// new "assembled" state. Phones fire resize on returning to the tab (address bar), so debounce.
let rebuildTimer = 0;
function scheduleRebuild() {
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(() => {
    tl.progress(0); tl.kill();
    measureBeats();
    tl = buildTimeline();
    tl.progress(progress);
  }, 150);
}
addEventListener("resize", () => {
  renderer.setSize(innerWidth, innerHeight);
  composer.setSize(innerWidth, innerHeight);
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  scheduleRebuild();
});
applyParts();
updateCables();
// compile every material up front: parts that first enter the frame mid-scroll would otherwise stall it
await renderer.compileAsync(scene, camera);
composer.render();
document.getElementById("loader").classList.add("done");
const introTl = REDUCED ? null : intro();
const clock = new THREE.Clock();
let lookPan = 0, lookTilt = 0;

renderer.setAnimationLoop(() => {
  const dt = Math.min(clock.getDelta(), 1 / 30);
  // critically damped spring toward the scroll goal (no overshoot, frame-rate independent)
  const w = 9, x = progress - goal, e = Math.exp(-w * dt), t0 = (vel + w * x) * dt;
  const next = goal + (x + t0) * e, cap = MAX_RATE * dt;
  vel = (vel - w * t0) * e;
  if (Math.abs(next - progress) > cap) { progress += Math.sign(next - progress) * cap; vel = Math.sign(vel) * Math.min(Math.abs(vel), MAX_RATE); }
  else progress = next;
  if (Math.abs(progress - goal) < 1e-5 && Math.abs(vel) < 1e-5) { progress = goal; vel = 0; }
  if (!introTl || !introTl.isActive()) tl.progress(progress);
  // in the hero and CTA beats the head follows the cursor, gently
  const idle = progress < at(1) - 0.07 || progress > at(5) - 0.02;
  lookPan += ((idle ? mouse.x * 28 : 0) - lookPan) * Math.min(1, dt * 3);
  lookTilt += ((idle ? -mouse.y * 14 : 0) - lookTilt) * Math.min(1, dt * 3);
  const base = { pan: state.pan, tilt: state.tilt };
  state.pan += lookPan; state.tilt += lookTilt;
  applyParts();
  state.pan = base.pan; state.tilt = base.tilt;
  updateCables();
  panArc.material.opacity = tiltArc.material.opacity = state.arcs * 0.8;
  // camera with inertia + a touch of parallax: feels hand-held, like a game camera
  const k = 1 - Math.exp(-dt * 12);
  smooth.pos.lerp(tmp.copy(cam.pos).add(new THREE.Vector3(mouse.x * 0.006, -mouse.y * 0.004, 0)), k);
  smooth.target.lerp(cam.target, k);
  smooth.fov += (cam.fov - smooth.fov) * k;
  camera.position.copy(smooth.pos);
  camera.fov = smooth.fov;
  camera.updateProjectionMatrix();
  camera.lookAt(smooth.target);
  bokeh.uniforms.focus.value = camera.position.distanceTo(smooth.target);
  ledLight.position.copy(b2t(...rig.led));
  updateChrome();
  updateReadout(pan.rotation.y * THREE.MathUtils.RAD2DEG, tilt.rotation.x * THREE.MathUtils.RAD2DEG);
  composer.render();
});
