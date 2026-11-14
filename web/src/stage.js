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
import { restyle, cableMaterial, powerCableMaterial } from "./materials.js";
import { DepthOfFieldPass } from "./dof.js";
import { siliconeMat, MAT_THICKNESS } from "./bench.js";

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
  renderer.setPixelRatio(Math.min(devicePixelRatio, TOUCH ? 1.25 : 1.5));
  renderer.setSize(canvas.clientWidth || innerWidth, canvas.clientHeight || innerHeight, false);   // CSS sizes the canvas (100lvh)
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
  // the status LED's spill: it sits inside the lid and point lights cast no shadow here, so it is kept
  // faint and short, or it shines straight through the shell onto the yoke. Rides on the head, see below.
  const ledLight = new THREE.PointLight(0x40ff88, 0, 0.035, 2);

  // ------------------------------------------------------------------ loading
  const manager = new THREE.LoadingManager();
  manager.onProgress = (_, done, total) => onProgress?.(done / total);
  const draco = new DRACOLoader(manager).setDecoderPath("./draco/");
  const gltf = new GLTFLoader(manager).setDRACOLoader(draco);
  const [lynx, set, rig, env] = await Promise.all([
    gltf.loadAsync("./assets/lynx.glb"),
    gltf.loadAsync("./assets/set.glb"),
    fetch("./assets/rig.json").then((r) => r.json()),
    new HDRLoader(manager).loadAsync("./assets/workshop_512.hdr"),
    // the mat's and the base's print is drawn with the page's fonts
    document.fonts.load('600 16px "Martian Mono"'), document.fonts.load("800 16px Archivo"),
  ]);
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = scene.background = pmrem.fromEquirectangular(env).texture;
  env.dispose();
  pmrem.dispose();

  restyle(set.scene);
  const anisotropy = renderer.capabilities.getMaxAnisotropy();   // the mat and desk are seen at grazing angles
  // the room is the HDRI now, and the flat ESD mat gives way to the silicone one
  for (const n of ["Wall", "ESDMat"]) set.scene.getObjectByName(n).visible = false;
  const mat = siliconeMat({ anisotropy });
  mat.position.set(0.02, set.scene.getObjectByName("Desk").position.y, -0.03);
  set.scene.add(mat);
  // the chips and caliper lay on the old mat: put them on the new one (it is a little thicker)
  const lift = mat.position.y + MAT_THICKNESS - set.scene.getObjectByName("ESDMat").position.y;
  set.scene.traverse((o) => { if (/^(Chip|Caliper)/.test(o.name)) o.position.y += lift; });
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
    led.emissiveIntensity = amount * 6;
    lightPipe.emissiveIntensity = amount * 1.2;
    ledLight.intensity = amount * 0.003;
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
  // the label printed on the pan base's front panel (33 x 16 mm at z = -3.9 mm), riding with the base
  const label = baseLabel(anisotropy);
  label.position.copy(b2t(20.5, 3.8, -49));
  holders.PanBase.attach(label);
  tilt.add(ledLight);
  ledLight.position.copy(tilt.worldToLocal(b2t(...rig.led)));
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
    // writes depth so depth of field keeps it sharp at the pivot; hidden while faded out (see setArcs)
    return new THREE.Line(g, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0 }));
  }
  const panArc = arc(0.05, -Math.PI / 2 - Math.PI / 2, -Math.PI / 2 + Math.PI / 2, 0x7fd6a4);
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

  // Adaptive resolution: frames rendered back to back are timed, and if they run below ~35 fps for a
  // second and a half the pixel ratio steps down (to 0.75 at the lowest). It never steps back up within
  // a visit: behind vsync, a fast GPU and a just-fast-enough one look the same.
  let lastFrame = -Infinity, slowFor = 0;
  function render(time = performance.now()) {
    const dt = time - lastFrame;
    lastFrame = time;
    if (dt < 100) {                                  // a consecutive frame, not the first after idling
      slowFor = dt > 28 ? slowFor + dt : Math.max(0, slowFor - dt);
      const ratio = renderer.getPixelRatio();
      if (slowFor > 1500 && ratio > 0.75) {
        slowFor = 0;
        renderer.setPixelRatio(ratio - 0.25);
        composer.setPixelRatio(ratio - 0.25);
      }
    }
    composer.render();
  }

  return {
    renderer, scene, camera, key, lynx, set, rig, mats, node, pan, tilt, holders, restPos, setLed,
    EXPL, explode, state, LIFT_MM, applyParts, groupNode, cables, updateCables, tmp, setArcs,
    composer, render, ao, dof,
  };
}

// Light-grey print on the gunmetal, like the finished-hardware renders: PAN/TILT, the eye mark, REV D.
function baseLabel(anisotropy) {
  const W = 30, H = 12, PX = 24;                     // millimetres, pixels per millimetre
  const c = document.createElement("canvas");
  c.width = W * PX; c.height = H * PX;
  const g = c.getContext("2d");
  g.fillStyle = g.strokeStyle = "rgba(214,218,224,0.86)";
  g.textBaseline = "middle";
  g.font = `600 ${2 * PX}px "Martian Mono", ui-monospace, monospace`;
  g.textAlign = "left"; g.fillText("PAN/TILT", 1.2 * PX, H * PX / 2);
  g.textAlign = "right"; g.fillText("REV D", (W - 1.2) * PX, H * PX / 2);
  g.lineWidth = 0.45 * PX;                           // the eye mark between them
  g.beginPath(); g.arc(W * PX / 2, H * PX / 2, 2.1 * PX, 0, Math.PI * 2); g.stroke();
  g.beginPath(); g.arc(W * PX / 2, H * PX / 2, 0.8 * PX, 0, Math.PI * 2); g.fill();
  const map = new THREE.CanvasTexture(c);
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = anisotropy;
  const material = new THREE.MeshStandardMaterial({ map, transparent: true, roughness: 0.6, depthWrite: false,
    polygonOffset: true, polygonOffsetFactor: -2 });
  return new THREE.Mesh(new THREE.PlaneGeometry(W * MM, H * MM), material);
}
