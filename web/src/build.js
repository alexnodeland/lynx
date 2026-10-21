// SPDX-License-Identifier: MIT
// The build guide: the same scene as the landing page, driven by steps instead of scroll.
// Parts from earlier steps sit assembled, the current step's parts fly in, and everything still
// to come is a faint ghost of the finished camera. Drag to orbit; ← → or the rail to step.
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import gsap from "gsap";
import { createStage, b2l, b2t, REST_TILT } from "./stage.js";
import { GUIDE, PRINTED } from "./guide.js";

const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;
const bar = document.querySelector("#loader .bar span");
const {
  renderer, scene, camera, rig, pan, tilt, holders, restPos, led, ledLight, cables, updateCables, composer, bokeh, LIFT_MM,
} = await createStage({ canvas: document.getElementById("scene"), onProgress: (f) => (bar.style.width = `${100 * f}%`) });

// ------------------------------------------------------------------ parts: solid, arriving, ghost, hidden
const PLUG = "Jig_USBC_PowerPlug";
const PARTS = Object.keys(holders).filter((k) => k !== PLUG && k !== "Paperclip");
const stepOf = {};
GUIDE.forEach((s, i) => (s.add || []).forEach((k) => (stepOf[k] ??= i)));
const meshes = {};
for (const k of Object.keys(holders)) {
  meshes[k] = [];
  holders[k].traverse((o) => {
    if (o.isMesh) {
      o.userData.mat = o.material;
      o.userData.shadow = o.castShadow;
      meshes[k].push(o);
    }
  });
}
const ghost = new THREE.MeshBasicMaterial({ color: 0x9fe3bd, transparent: true, opacity: 0.075, depthWrite: false });
function look(k, mode) {
  const h = holders[k];
  if (!h) return;
  h.visible = mode !== "hidden";
  for (const m of meshes[k]) {
    const g = mode === "ghost";
    m.material = g ? ghost : m.userData.mat;
    m.castShadow = !g && m.userData.shadow;
    m.renderOrder = g ? 2 : 0;
  }
}

// highlight: a pulsing additive overlay on the parts a step is about, and a glowing cable while it's routed
const glow = new THREE.MeshBasicMaterial({ color: 0x7fd6a4, transparent: true, opacity: 0, depthWrite: false,
  blending: THREE.AdditiveBlending });
const cableGlow = new THREE.MeshStandardMaterial({ color: 0x7fd6a4, emissive: 0x7fd6a4, emissiveIntensity: 1.4, roughness: 0.4 });
const cableMat = cables[0].material;
let glowTl = null;
function highlight(keys) {
  glowTl?.kill();
  glow.opacity = 0;
  for (const k of Object.keys(meshes))
    for (const m of meshes[k]) {
      const on = keys.includes(k);
      if (on && !m.userData.glow) { m.userData.glow = new THREE.Mesh(m.geometry, glow); m.userData.glow.renderOrder = 3; m.add(m.userData.glow); }
      if (m.userData.glow) m.userData.glow.visible = on;
    }
  if (keys.length) glowTl = gsap.to(glow, { opacity: 0.32, duration: 0.9, repeat: -1, yoyo: true, ease: "sine.inOut", delay: 0.8 });
}

// how far each part is from home (0 = assembled), along its own `from` vector
const off = Object.fromEntries(Object.keys(holders).map((k) => [k, { amt: 0, v: [0, 0, 0] }]));
const LAYOUT = {   // "print" step: the eight printed parts, spread out like a plate of fresh prints
  Lid: [0, 0, 46], LightPipePrint: [0, -22, 38], Base: [0, 0, 18], BatteryTray: [0, 40, 8], Yoke: [0, 0, 0],
  PanBase: [0, 0, -40], PanCap: [0, 34, -14], ServoShroud: [46, 0, 0],
};
const pose = { pan: 0, tilt: 0, lift: 0, plug: 1, led: 0, cableIn: 0, cableOut: 0 };

function apply() {
  for (const [k, o] of Object.entries(off)) {
    const h = holders[k];
    if (!h || k === PLUG) continue;
    h.position.copy(restPos[k]).add(b2l(o.v[0] * o.amt, o.v[1] * o.amt, o.v[2] * o.amt));
  }
  if (holders[PLUG]) holders[PLUG].position.copy(restPos[PLUG]).add(b2l(0, 26 * pose.plug, 0));
  tilt.position.y = rig.pivots.tz + LIFT_MM * pose.lift;
  pan.rotation.y = THREE.MathUtils.degToRad(pose.pan);
  tilt.rotation.x = THREE.MathUtils.degToRad(pose.tilt);
  led.emissiveIntensity = pose.led * 6;
  ledLight.intensity = pose.led * 0.02;
  ledLight.position.copy(b2t(...rig.led));
  // cables "draw" along their route as they're laid in
  cables.forEach((c, i) => {
    const f = i === 0 ? pose.cableIn : pose.cableOut;
    c.visible = f > 0.001;
    const n = c.geometry.index ? c.geometry.index.count : 0;
    if (n) c.geometry.setDrawRange(0, Math.ceil((f * n) / 6) * 6);
  });
}

// ------------------------------------------------------------------ camera: frame what matters, leave room for the card
const controls = new OrbitControls(camera, renderer.domElement);
Object.assign(controls, { enableDamping: true, dampingFactor: 0.08, minDistance: 0.09, maxDistance: 0.9, maxPolarAngle: 1.5,
  rotateSpeed: 0.7, zoomSpeed: 0.8 });
let userMoved = false;
controls.addEventListener("start", () => { userMoved = true; camTl?.kill(); });

const box = new THREE.Box3(), sphere = new THREE.Sphere(), tmpBox = new THREE.Box3();
function frame(keys, { az = -32, el = 24, pad = 2.1, min = 0.24 } = {}) {
  apply();
  lynxUpdate();
  box.makeEmpty();
  for (const k of keys) for (const m of meshes[k] || []) box.union(tmpBox.setFromObject(m));
  if (box.isEmpty()) for (const k of PARTS) for (const m of meshes[k]) box.union(tmpBox.setFromObject(m));
  box.getBoundingSphere(sphere);
  const fov = THREE.MathUtils.degToRad(camera.fov);
  const dist = Math.max(min, (sphere.radius / Math.sin(fov / 2)) * pad);
  const a = THREE.MathUtils.degToRad(az), e = THREE.MathUtils.degToRad(el);
  const dir = new THREE.Vector3(Math.cos(e) * Math.sin(a), Math.sin(e), Math.cos(e) * Math.cos(a));
  return { target: sphere.center.clone(), pos: sphere.center.clone().addScaledVector(dir, dist) };
}
function lynxUpdate() { scene.updateMatrixWorld(true); }

let camTl = null;
function flyTo({ target, pos }, dur = 1.1) {
  camTl?.kill();
  userMoved = false;
  camTl = gsap.timeline();
  camTl.to(controls.target, { x: target.x, y: target.y, z: target.z, duration: dur, ease: "power2.inOut" }, 0);
  camTl.to(camera.position, { x: pos.x, y: pos.y, z: pos.z, duration: dur, ease: "power2.inOut" }, 0);
}

// the card covers part of the screen: shift the projection so the model sits in the free space
function layout() {
  const w = innerWidth, h = innerHeight;
  renderer.setSize(w, h);
  composer.setSize(w, h);
  camera.aspect = w / h;
  const card = document.getElementById("card").getBoundingClientRect();
  if (w > 760) camera.setViewOffset(w, h, -Math.min(card.right * 0.5, w * 0.22), 0, w, h);
  else camera.setViewOffset(w, h, 0, Math.min(card.height * 0.45, h * 0.24), w, h);
  camera.updateProjectionMatrix();
}
addEventListener("resize", layout);

// ------------------------------------------------------------------ steps
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
let cur = -1, stepTl = null, idleTl = null;

function fillCard(s, i) {
  $("g-chapter").textContent = s.chapter;
  $("g-num").textContent = `step ${String(i + 1).padStart(2, "0")} / ${String(GUIDE.length).padStart(2, "0")}`;
  $("g-time").textContent = s.time || "";
  $("g-title").textContent = s.title;
  $("g-body").textContent = s.body.replace(/\s+/g, " ").trim();
  $("g-need").innerHTML = (s.need || []).map(([q, n, d]) => `<li><b>${esc(q)}×</b><span>${esc(n)}${d ? `<small>${esc(d)}</small>` : ""}</span></li>`).join("");
  $("g-list").innerHTML = (s.list || []).map((l) => `<li>${esc(l)}</li>`).join("");
  $("g-tools").innerHTML = (s.tools || []).map((t) => `<span>${esc(t)}</span>`).join("");
  $("g-tip").textContent = s.tip || "";
  const link = $("g-link");
  link.textContent = s.link ? s.link[0] : "";
  link.href = s.link ? s.link[1] : "#";
  link.target = s.link && s.link[1].startsWith("http") ? "_blank" : "_self";
  $("prev").disabled = i === 0;
  const nx = GUIDE[i + 1];
  $("next-label").textContent = nx ? `Next: ${nx.title}` : "See it in action";
  document.querySelectorAll("#steps-rail button").forEach((b, j) => {
    b.classList.toggle("now", j === i);
    b.classList.toggle("done", j < i);
    b.setAttribute("aria-current", j === i ? "step" : "false");
  });
  document.querySelectorAll("#steps-rail .ch").forEach((c) => c.classList.toggle("on", c.dataset.ch === s.chapter));
}

function go(i, { animate = true } = {}) {
  i = Math.max(0, Math.min(GUIDE.length - 1, i));
  const s = GUIDE[i];
  const first = cur < 0;
  cur = i;
  history.replaceState(null, "", `#${s.id}`);
  const card = $("card");
  card.classList.add("swap");
  setTimeout(() => { fillCard(s, i); card.classList.remove("swap"); }, first ? 0 : 180);

  stepTl?.kill();
  idleTl?.kill();
  const mountAt = GUIDE.findIndex((g) => g.mount), cableAt = GUIDE.findIndex((g) => g.cable);
  const all = s.show === "all", printed = s.show === "printed", only = Array.isArray(s.show) ? s.show : null;

  // the final pose of this step, then what moves into it
  for (const k of PARTS) {
    off[k].amt = 0;
    let mode;
    if (all) mode = "solid";
    else if (printed) mode = PRINTED.includes(k) ? "solid" : "hidden";
    else if (only) mode = only.includes(k) ? "solid" : "hidden";
    else mode = stepOf[k] === undefined || stepOf[k] > i ? "ghost" : "solid";
    look(k, mode);
    if (printed && LAYOUT[k]) { off[k].v = LAYOUT[k]; off[k].amt = 1; }
  }
  look(PLUG, s.plug || s.id === "start" ? "solid" : "hidden");
  Object.assign(pose, {
    pan: 0,
    tilt: all ? REST_TILT : 0,
    lift: s.lift || printed ? 1 : 0,
    plug: s.id === "start" ? 0 : 1,
    led: all || s.action === "led" ? 1 : 0,
    cableIn: all || (cableAt >= 0 && i > cableAt) ? 1 : 0,
    cableOut: s.id === "start" ? 1 : 0,
  });
  const arriving = (s.add || []).filter((k) => holders[k]);
  highlight(s.focus && s.focus !== "all" ? s.focus : []);
  cables[0].material = s.cable ? cableGlow : cableMat;
  const focusKeys = s.focus === "all" ? PARTS : s.focus || (arriving.length ? arriving : only || (printed ? PRINTED : PARTS));
  const shot = frame(focusKeys, s.frame || (all || printed ? { pad: 1.6, el: 20, min: 0.3 } : {}));
  if (first || !animate || REDUCED) {
    camera.position.copy(shot.pos);
    controls.target.copy(shot.target);
  } else flyTo(shot);

  if (REDUCED || !animate) { apply(); return; }
  stepTl = gsap.timeline({ delay: first ? 0.2 : 0.35 });
  arriving.forEach((k, n) => {
    off[k].v = (s.from && s.from[k]) || [0, 0, 30];
    off[k].amt = 1;
    stepTl.to(off[k], { amt: 0, duration: 0.9, ease: "power3.out" }, n * 0.16);
  });
  if (s.mount) { pose.lift = 1; stepTl.to(pose, { lift: 0, duration: 1.1, ease: "power3.inOut" }, 0.5); }
  if (s.cable) { pose.cableIn = 0; stepTl.to(pose, { cableIn: 1, duration: 2.2, ease: "power1.inOut" }, 0.6); }
  if (s.plug) {
    pose.plug = 1;
    pose.cableOut = 1;
    pose.tilt = 0;
    stepTl.to(pose, { plug: 0, duration: 0.8, ease: "power2.inOut" }, 0.2);
    stepTl.to(pose, { led: 1, duration: 0.15 }, 1.05);
    stepTl.to(pose, { tilt: REST_TILT, duration: 1.0, ease: "back.out(1.4)" }, 1.25);
  }
  if (s.action === "led") stepTl.fromTo(pose, { led: 0 }, { led: 1, duration: 0.3, repeat: 3, yoyo: true }, 0.4);
  if (s.action === "look" || s.id === "start") {
    // once assembled it looks around, the way it does in the video
    stepTl.add(() => {
      idleTl = gsap.timeline({ repeat: -1, repeatDelay: 1.2, delay: 0.6 });
      for (const [p, t] of [[-34, 52], [28, 38], [0, REST_TILT]]) idleTl.to(pose, { pan: p, tilt: t, duration: 1.1, ease: "back.inOut(1.2)" }, "+=0.9");
    }, s.plug ? 2.4 : 0.4);
  }
}

function buildRail() {
  const rail = $("steps-rail");
  const chapters = [];
  GUIDE.forEach((s, i) => {
    let c = chapters.at(-1);
    if (!c || c.name !== s.chapter) chapters.push((c = { name: s.chapter, steps: [] }));
    c.steps.push([s, i]);
  });
  rail.innerHTML = chapters.map((c) => `<div class="ch" data-ch="${esc(c.name)}"><small>${esc(c.name)}</small><div class="dots">${
    c.steps.map(([s, i]) => `<button data-i="${i}" aria-label="Step ${i + 1}: ${esc(s.title)}" title="${esc(s.title)}"></button>`).join("")
  }</div></div>`).join("");
  rail.addEventListener("click", (e) => { const b = e.target.closest("button"); if (b) go(+b.dataset.i); });
}

buildRail();
$("prev").onclick = () => go(cur - 1);
$("next").onclick = () => (cur === GUIDE.length - 1 ? (location.href = "./") : go(cur + 1));
$("replay").onclick = () => go(cur);
addEventListener("keydown", (e) => {
  if (e.target.closest("input, textarea")) return;
  if (e.key === "ArrowRight" || e.key === "PageDown") go(cur + 1);
  else if (e.key === "ArrowLeft" || e.key === "PageUp") go(cur - 1);
  else if (e.key === "r" || e.key === "R") go(cur);
  else if (e.key === "Home") go(0);
  else if (e.key === "End") go(GUIDE.length - 1);
});
addEventListener("hashchange", () => { const j = GUIDE.findIndex((s) => `#${s.id}` === location.hash); if (j >= 0 && j !== cur) go(j); });
// the card swaps glass tilt toward the cursor, like the landing page
const card = document.querySelector("#card .face");
addEventListener("pointermove", (e) => {
  const r = card.getBoundingClientRect();
  const dx = THREE.MathUtils.clamp((e.clientX - (r.left + r.width / 2)) / (r.width * 1.6), -1, 1);
  const dy = THREE.MathUtils.clamp((e.clientY - (r.top + r.height / 2)) / (r.height * 1.6), -1, 1);
  card.style.setProperty("--ry", `${dx * 4}deg`);
  card.style.setProperty("--rx", `${-dy * 4}deg`);
  card.style.setProperty("--mx", `${((e.clientX - r.left) / r.width) * 100}%`);
  card.style.setProperty("--my", `${((e.clientY - r.top) / r.height) * 100}%`);
});

// ------------------------------------------------------------------ run
layout();
const start = Math.max(0, GUIDE.findIndex((s) => `#${s.id}` === location.hash));
apply();
updateCables();
await renderer.compileAsync(scene, camera);
go(start, { animate: true });
document.getElementById("loader").classList.add("done");
renderer.setAnimationLoop(() => {
  controls.update();
  apply();
  updateCables();
  bokeh.uniforms.focus.value = camera.position.distanceTo(controls.target);
  composer.render();
});
