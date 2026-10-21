// SPDX-License-Identifier: MIT
// lynx landing page: one immersive scene, driven by scroll.
//   hero: the camera self-assembles and looks at your cursor
//   usb: it turns, the USB-C goes into the base, the LED wakes
//   sd: the microSD slides in
//   motion: pan/tilt sweep its full range, with range arcs
//   assembly: scroll explodes it and puts it back together, step by step
//   cta: back to the hero, with the calls to action
import * as THREE from "three";
import gsap from "gsap";
import { createStage, b2t, REST_TILT } from "./stage.js";

const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;

// ------------------------------------------------------------------ scene (shared with the build guide)
const bar = document.querySelector("#loader .bar span");
const {
  renderer, scene, camera, ledLight, rig, pan, tilt, holders, EXPL, explode, state, applyParts, groupNode,
  updateCables, tmp, panArc, tiltArc, composer, bokeh,
} = await createStage({ canvas: document.getElementById("scene"), onProgress: (f) => (bar.style.width = `${100 * f}%`) });
const ASSEMBLY = [
  ["Jig_Magnet", "Magnet into the base"], ["Jig_Servo_Pan", "Pan servo, two screws"], ["PanCap", "Pan cap, two side screws"],
  ["Jig_Horn_Pan", "Yoke onto the pan horn"], ["Jig_Servo_Tilt", "Tilt servo into the yoke"], ["ServoShroud", "Finned shroud over it"],
  ["Jig_LiPo_401723", "Battery"], ["BatteryTray", "Tray clicks in"], ["Jig_XIAO_Sense", "Board onto the tray"],
  ["Lid", "Light pipe, lid, two screws"],
];
document.getElementById("steps").innerHTML = ASSEMBLY.map(([, t]) => `<li>${t}</li>`).join("");

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
  const H = document.documentElement.scrollHeight - viewH, y = (goal * H + viewH * 0.45) / H;
  beatAt.forEach((t, i) => { if (t <= y) cur = i; });
  railLinks.forEach((a) => a.classList.toggle("on", +a.dataset.to === cur || (cur === 5 && +a.dataset.to === 4)));
  // leader line from the visible slab to its anchor
  const beat = beats[cur], key = beat.dataset.anchor, slab = beat.querySelector(".slab");
  if (!key || !slab || !slab.classList.contains("in") || (introTl && introTl.isActive())) { callout.classList.add("hide"); return; }
  const w = ANCHORS[key].n.localToWorld(tmp.copy(ANCHORS[key].local)).project(camera);
  const cv = renderer.domElement, tx = (w.x * 0.5 + 0.5) * cv.clientWidth, ty = (-w.y * 0.5 + 0.5) * cv.clientHeight;
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
const MAX_RATE = 0.3;   // timeline progress per second: caps how fast a fling can drive the scene
// read the scroll position straight off the page, once per frame in the render loop (no scroll-event work)
// viewport height held steady on phones, so the address bar sliding away doesn't nudge the scene
let viewH = innerHeight;
const readGoal = () => { const H = document.documentElement.scrollHeight - viewH; goal = H > 0 ? Math.min(1, Math.max(0, scrollY / H)) : 0; };
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
// Phones resize on every scroll as the address bar hides and shows. The canvas is sized to the large
// viewport (100lvh), so a height-only change needs nothing: resizing the GL buffers and rebuilding the
// timeline mid-fling is what made touch scrolling stutter.
let lastW = innerWidth;
addEventListener("resize", () => {
  if (TOUCH && innerWidth === lastW) return;
  lastW = innerWidth; viewH = innerHeight;
  const cv = renderer.domElement, w = cv.clientWidth, h = cv.clientHeight;
  renderer.setSize(w, h, false);
  composer.setSize(w, h);
  camera.aspect = w / h;
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
const parallax = new THREE.Vector3();

renderer.setAnimationLoop(() => {
  const dt = Math.min(clock.getDelta(), 1 / 30);
  readGoal();
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
  smooth.pos.lerp(tmp.copy(cam.pos).add(parallax.set(mouse.x * 0.006, -mouse.y * 0.004, 0)), k);
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
