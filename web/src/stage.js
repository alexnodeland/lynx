// SPDX-License-Identifier: MIT
// The shared lynx scene: renderer, studio lighting, the bench set, the product with its pan/tilt
// rig and per-part holders, the cables (rebuilt from CAD waypoints), range arcs and post-processing.
// Used by the landing page (main.js) and the build guide (build.js), so both show the same model.
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
import { restyle, cableMaterial, powerCableMaterial } from "./materials.js";

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
  renderer.toneMapping = THREE.AgXToneMapping;
  renderer.toneMappingExposure = 0.82;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x0b0b0d);
  scene.fog = new THREE.Fog(0x0b0b0d, 0.7, 1.9);
  const camera = new THREE.PerspectiveCamera(30, (canvas.clientWidth || innerWidth) / (canvas.clientHeight || innerHeight), 0.005, 5);

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
  const manager = new THREE.LoadingManager();
  manager.onProgress = (_, done, total) => onProgress?.(done / total);
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
  // explode amount per part (0 assembled .. 1 exploded); the tilt group lifts off the yoke as a whole
  const explode = Object.fromEntries(Object.keys(EXPL).map((k) => [k, 0]));
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
  if (!TOUCH) composer.addPass(ao);
  composer.addPass(new UnrealBloomPass(new THREE.Vector2(innerWidth, innerHeight), 0.25, 0.4, 0.92));
  // shallow depth of field like the renders: focus tracks the product every frame
  const bokeh = new BokehPass(scene, camera, { focus: 0.34, aperture: 0.0035, maxblur: 0.006 });
  if (!TOUCH) composer.addPass(bokeh);
  composer.addPass(new OutputPass());


  return {
    renderer, scene, camera, key, rim, ledLight, lynx, set, rig, mats, node, pan, tilt, holders, restPos, led,
    EXPL, explode, state, LIFT_MM, applyParts, groupNode, cables, updateCables, tmp, panArc, tiltArc,
    composer, ao, bokeh,
  };
}
