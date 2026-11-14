// SPDX-License-Identifier: MIT
// The bench's props at product-shot detail, built procedurally: the soldering station with its iron,
// holder and cable, and the solder and wire spools. They stand in for set.glb's placeholder blocks, in
// the same places, on the desk. Everything is in metres; each prop's origin is its footprint's centre.
import * as THREE from "three";
import { RoundedBoxGeometry } from "three/examples/jsm/geometries/RoundedBoxGeometry.js";

const P = (o) => new THREE.MeshPhysicalMaterial(o);

function canvasTexture(w, h, draw, { srgb = true, repeat = [1, 1] } = {}) {
  const c = document.createElement("canvas");
  c.width = w; c.height = h;
  draw(c.getContext("2d"), w, h);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(...repeat);
  t.anisotropy = 8;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// fine grain, as on moulded plastic: a height map for bump mapping
const grain = canvasTexture(256, 256, (g, w, h) => {
  const img = g.createImageData(w, h);
  for (let i = 0; i < img.data.length; i += 4) {
    const v = 118 + Math.random() * 20;
    img.data[i] = img.data[i + 1] = img.data[i + 2] = v; img.data[i + 3] = 255;
  }
  g.putImageData(img, 0, 0);
}, { srgb: false, repeat: [6, 6] });

// knurling around a knob or a grip (lathe u runs around)
const knurl = canvasTexture(64, 8, (g, w, h) => {
  for (let x = 0; x < w; x++) { const v = 128 + 110 * Math.cos((x / w) * Math.PI * 2 * 4); g.fillStyle = `rgb(${v},${v},${v})`; g.fillRect(x, 0, 1, h); }
}, { srgb: false, repeat: [12, 1] });

// wire wound on a spool: one groove per turn (lathe v runs along the spool's height)
const winding = canvasTexture(8, 64, (g, w, h) => {
  for (let y = 0; y < h; y++) { const v = 128 + 120 * Math.cos((y / h) * Math.PI * 2 * 4); g.fillStyle = `rgb(${v},${v},${v})`; g.fillRect(0, y, w, 1); }
}, { srgb: false });

const lathe = (points, segments = 64) => new THREE.LatheGeometry(points.map(([r, y]) => new THREE.Vector2(r, y)), segments);

// ------------------------------------------------------------------ soldering station
const W = 0.11, H = 0.088, D = 0.12, FOOT = 0.004;
// the faceplate (94 x 72 mm), laid out from its top-left corner: x right, y down, in metres
const PLATE = { w: 0.094, h: 0.072 };
const LCD = { x: 0.04, y: 0.024, w: 0.046, h: 0.018 };
const KNOB = { x: 0.047, y: 0.053, r: 0.0105 };
const SWITCH = { x: 0.012, y: 0.06 }, SOCKET = { x: 0.082, y: 0.06 }, LED = { x: 0.078, y: 0.024 };
// faceplate position -> station-local
const onPlate = ({ x, y }, z) => new THREE.Vector3(x - PLATE.w / 2, FOOT + H / 2 + PLATE.h / 2 - y, D / 2 + z);

function faceplateArt(g, w, h) {
  const s = w / 0.094;                               // pixels per metre on the faceplate
  g.fillStyle = "#e2a51d"; g.fillRect(0, 0, w, h);
  g.fillStyle = "rgba(255,255,255,.08)"; g.fillRect(0, 0, w, h * 0.5);
  g.fillStyle = "#1c1406";
  g.font = `700 ${0.0042 * s}px Archivo, system-ui, sans-serif`;
  g.fontStretch = "expanded";
  g.fillText("SOLDERING STATION", 0.006 * s, 0.0085 * s);
  g.font = `500 ${0.0026 * s}px "Martian Mono", ui-monospace, monospace`;
  g.fillText("60 W · ESD SAFE", 0.006 * s, 0.0122 * s);
  // the temperature dial around the knob
  const cx = KNOB.x * s, cy = KNOB.y * s, r = (KNOB.r + 0.0012) * s;
  g.strokeStyle = "#1c1406"; g.lineWidth = 0.0005 * s;
  g.textAlign = "center"; g.textBaseline = "middle";
  g.font = `600 ${0.0024 * s}px "Martian Mono", ui-monospace, monospace`;
  for (let i = 0; i <= 10; i++) {
    const a = Math.PI * (0.75 + 1.5 * (i / 10)), major = i % 2 === 0;
    g.beginPath();
    g.moveTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    g.lineTo(cx + Math.cos(a) * (r + (major ? 0.0028 : 0.0016) * s), cy + Math.sin(a) * (r + (major ? 0.0028 : 0.0016) * s));
    g.stroke();
    if (major) g.fillText(String(200 + i * 25), cx + Math.cos(a) * (r + 0.0052 * s), cy + Math.sin(a) * (r + 0.0052 * s));
  }
  g.font = `600 ${0.0026 * s}px "Martian Mono", ui-monospace, monospace`;
  g.fillText("°C", cx, cy + r + 0.0035 * s);
  g.fillText("I/O", SWITCH.x * s, (SWITCH.y + 0.0095) * s);
  g.fillText("IRON", SOCKET.x * s, (SOCKET.y + 0.0095) * s);
}

function lcdArt(g, w, h) {
  g.fillStyle = "#120806"; g.fillRect(0, 0, w, h);
  g.fillStyle = "#ff5a24";
  g.textBaseline = "middle";
  g.font = `900 ${h * 0.72}px Doto, "Martian Mono", monospace`;
  g.fillText("350", w * 0.06, h * 0.54);
  g.font = `700 ${h * 0.26}px "Martian Mono", monospace`;
  g.fillText("°C", w * 0.76, h * 0.36);
  g.fillStyle = "rgba(255,90,36,.55)";
  g.font = `600 ${h * 0.17}px "Martian Mono", monospace`;
  g.fillText("SET", w * 0.76, h * 0.74);
}

export function solderingStation() {
  const station = new THREE.Group();
  const blue = P({ color: 0x3549b4, roughness: 0.42, clearcoat: 0.4, clearcoatRoughness: 0.35, bumpMap: grain, bumpScale: 0.35 });
  const black = P({ color: 0x141518, roughness: 0.5, bumpMap: grain, bumpScale: 0.3 });
  const yellow = P({ color: 0xe2a51d, roughness: 0.38, clearcoat: 0.5, clearcoatRoughness: 0.2 });
  const face = P({ map: canvasTexture(1024, 768, faceplateArt), roughness: 0.38, clearcoat: 0.5, clearcoatRoughness: 0.2 });
  const add = (geometry, material, x, y, z) => {
    const m = new THREE.Mesh(geometry, material);
    m.position.set(x, y, z);
    m.castShadow = m.receiveShadow = true;
    station.add(m);
    return m;
  };
  const cy = FOOT + H / 2;
  add(new RoundedBoxGeometry(W, H, D, 5, 0.01), blue, 0, cy, 0);
  // the front: a black bezel, the printed faceplate (+z face only), the LCD behind its window
  add(new RoundedBoxGeometry(W - 0.008, H - 0.01, 0.004, 3, 0.003), black, 0, cy, D / 2 - 0.001);
  add(new RoundedBoxGeometry(0.094, 0.072, 0.003, 3, 0.002), [yellow, yellow, yellow, yellow, face, yellow], 0, cy, D / 2 + 0.0012);
  // the LCD: a black frame proud of the faceplate, the lit display in front of it
  const lcdMap = canvasTexture(512, 200, lcdArt);
  add(new RoundedBoxGeometry(LCD.w + 0.004, LCD.h + 0.004, 0.0012, 2, 0.0005), black, ...onPlate(LCD, 0.0031).toArray());
  add(new THREE.PlaneGeometry(LCD.w, LCD.h), new THREE.MeshStandardMaterial({ map: lcdMap, emissiveMap: lcdMap, emissive: 0xffffff,
    emissiveIntensity: 1.1, roughness: 0.15 }), ...onPlate(LCD, 0.0038).toArray());
  // the knob, knurled, with its pointer
  const r = KNOB.r;
  const knob = add(lathe([[0, 0], [r - 0.0005, 0], [r, 0.0008], [r, 0.009], [r - 0.0012, 0.0115], [0.004, 0.012], [0, 0.012]], 48),
    P({ color: 0x232428, roughness: 0.45, bumpMap: knurl, bumpScale: 1.2, clearcoat: 0.3 }), ...onPlate(KNOB, 0.0027).toArray());
  knob.rotation.x = Math.PI / 2;
  // the pointer: a white line on the knob's face, from the centre out (the face is the lathe's top, y up)
  const pointer = new THREE.Mesh(new THREE.BoxGeometry(0.0012, 0.0004, 0.0062), P({ color: 0xf2f2ee, roughness: 0.4 }));
  pointer.position.set(0, 0.0121, -r / 2);
  knob.add(pointer);
  knob.rotation.z = 0.5;                             // set to about 350
  // rocker switch, power LED, the iron's socket
  add(new RoundedBoxGeometry(0.009, 0.013, 0.004, 2, 0.0015), black, ...onPlate(SWITCH, 0.003).toArray());
  add(new THREE.SphereGeometry(0.0013, 12, 8), new THREE.MeshStandardMaterial({ color: 0x401010, emissive: 0xff2a1a, emissiveIntensity: 2.4 }),
    ...onPlate(LED, 0.0026).toArray());
  const socket = add(lathe([[0, 0], [0.0062, 0], [0.0064, 0.0005], [0.0064, 0.004], [0.0045, 0.0045], [0.0045, 0.0025], [0, 0.0025]], 32),
    P({ color: 0x1a1b1e, metalness: 0.4, roughness: 0.35 }), ...onPlate(SOCKET, 0.0018).toArray());
  socket.rotation.x = Math.PI / 2;
  for (const [x, z] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) add(new THREE.CylinderGeometry(0.006, 0.0065, FOOT, 20), black, x * (W / 2 - 0.012), FOOT / 2, z * (D / 2 - 0.014));
  station.userData.socket = onPlate(SOCKET, 0.006);
  return station;
}

// ------------------------------------------------------------------ iron and holder
export function ironHolder() {
  const holder = new THREE.Group();
  const add = (geometry, material, parent = holder) => {
    const m = new THREE.Mesh(geometry, material);
    m.castShadow = m.receiveShadow = true;
    parent.add(m);
    return m;
  };
  const cast = P({ color: 0x1b1c1f, metalness: 0.55, roughness: 0.42, bumpMap: grain, bumpScale: 0.4 });
  const chrome = P({ color: 0xe4e6ea, metalness: 1, roughness: 0.12 });
  // a weighted base with a recessed tray and a damp sponge in it
  add(lathe([[0, 0], [0.034, 0], [0.035, 0.0015], [0.033, 0.009], [0.029, 0.0105], [0.026, 0.008], [0, 0.008]], 64), cast);
  const sponge = add(new RoundedBoxGeometry(0.036, 0.006, 0.02, 3, 0.002),
    P({ color: 0xc89a3c, roughness: 0.95, bumpMap: grain, bumpScale: 2.5, sheen: 0.5, sheenColor: 0x6b4a10 }));
  sponge.position.set(0, 0.0105, 0.012);
  // the coil the iron rests in, leaning back, on a short post
  const coil = new THREE.Group();
  coil.position.set(0, 0.012, -0.006);
  coil.rotation.x = -0.62;
  holder.add(coil);
  add(new THREE.CylinderGeometry(0.004, 0.006, 0.016, 20), cast, coil).position.y = 0.006;
  const helix = new THREE.Curve();
  helix.getPoint = (t, out = new THREE.Vector3()) => {
    const a = t * Math.PI * 2 * 11, r = 0.0075 + 0.006 * t;
    return out.set(Math.cos(a) * r, 0.014 + t * 0.07, Math.sin(a) * r);
  };
  add(new THREE.TubeGeometry(helix, 600, 0.0009, 8), chrome, coil);
  // the iron, down the coil's axis: tinned tip and chrome shaft, then a ribbed grip and the cable boot
  const iron = new THREE.Group();
  iron.position.y = 0.03;
  coil.add(iron);
  add(lathe([[0, -0.012], [0.0011, -0.008], [0.0022, 0], [0.0024, 0.03], [0.0045, 0.034], [0, 0.034]], 40),
    P({ color: 0xc9ccd2, metalness: 1, roughness: 0.22 }), iron);
  add(lathe([
    [0, 0.033], [0.0058, 0.04],
    ...Array.from({ length: 9 }, (_, i) => [[0.0072, 0.046 + i * 0.006], [0.0066, 0.049 + i * 0.006]]).flat(),
    [0.0068, 0.1], [0.0058, 0.106], [0.0034, 0.118], [0.0026, 0.125], [0, 0.125],
  ], 40), P({ color: 0x2a3a8a, roughness: 0.5, clearcoat: 0.3, bumpMap: knurl, bumpScale: 0.6 }), iron);
  holder.userData.cableStart = () => iron.localToWorld(new THREE.Vector3(0, 0.125, 0));
  return holder;
}

// the iron's cable, from the back of its grip down to the bench and round to the station's socket
export function ironCable(from, to, deskY) {
  const mid = from.clone().lerp(to, 0.5);
  const curve = new THREE.CatmullRomCurve3([
    from, from.clone().add(new THREE.Vector3(0.004, -0.02, -0.02)), new THREE.Vector3(from.x + 0.01, deskY + 0.0021, from.z - 0.06),
    new THREE.Vector3(mid.x + 0.03, deskY + 0.0021, mid.z - 0.03),
    new THREE.Vector3(to.x + 0.02, deskY + 0.0021, to.z + 0.04), to.clone().add(new THREE.Vector3(0, -0.004, 0.012)), to,
  ], false, "centripetal");
  const cable = new THREE.Mesh(new THREE.TubeGeometry(curve, 200, 0.0021, 12), P({ color: 0x0e0e10, roughness: 0.55, sheen: 0.4, sheenColor: 0x2a2a2a }));
  cable.castShadow = cable.receiveShadow = true;
  return cable;
}

// ------------------------------------------------------------------ spools
const WIRES = {
  solder: () => P({ color: 0xcfd1d6, metalness: 1, roughness: 0.3, bumpMap: winding, bumpScale: 1.5 }),
  copper: () => P({ color: 0xd98a58, metalness: 1, roughness: 0.24, clearcoat: 0.7, clearcoatRoughness: 0.1, bumpMap: winding, bumpScale: 1.5 }),
  blue: () => P({ color: 0x1f45a6, roughness: 0.38, clearcoat: 0.4, bumpMap: winding, bumpScale: 1.5 }),
};

function labelArt(lines, band) {
  return (g, w, h) => {
    g.fillStyle = "#f2f1ec"; g.fillRect(0, 0, w, h);
    g.fillStyle = band; g.fillRect(0, h * 0.06, w, h * 0.1);
    g.fillStyle = "#18191c"; g.textAlign = "center"; g.textBaseline = "middle";
    g.font = `800 ${h * 0.075}px Archivo, system-ui, sans-serif`;
    g.fillText(lines[0], w / 2, h * 0.24);
    g.font = `500 ${h * 0.05}px "Martian Mono", ui-monospace, monospace`;
    lines.slice(1).forEach((l, i) => g.fillText(l, w / 2, h * (0.76 + i * 0.07)));
  };
}

// a spool on its side-less axis: flanges with rounded rims, the wound wire with a groove per turn, a
// printed label on the top flange
export function spool({ radius, height, core, hole, flange, wire, turns, label, band }) {
  const g = new THREE.Group();
  const t = Math.min(0.0028, height * 0.1);
  const flangeMat = P({ color: flange, roughness: 0.38, clearcoat: 0.3, clearcoatRoughness: 0.3, bumpMap: grain, bumpScale: 0.25 });
  const disc = lathe([[hole, 0], [radius - 0.0016, 0], [radius, 0.0009], [radius, t - 0.0009], [radius - 0.0016, t], [hole, t], [hole, 0]], 72);
  for (const y of [0, height - t]) {
    const f = new THREE.Mesh(disc, flangeMat);
    f.position.y = y;
    f.castShadow = f.receiveShadow = true;
    g.add(f);
  }
  const wireMat = WIRES[wire]();
  wireMat.bumpMap = wireMat.bumpMap.clone();
  wireMat.bumpMap.repeat.set(1, turns);
  const body = new THREE.Mesh(lathe([[core * 0.985, t], [core, t + 0.0015], [core * 1.01, height / 2], [core, height - t - 0.0015], [core * 0.985, height - t]], 72), wireMat);
  body.castShadow = body.receiveShadow = true;
  g.add(body);
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(hole, hole, height, 40, 1, true), P({ color: 0x2a2b2e, roughness: 0.6, side: THREE.BackSide }));
  hub.position.y = height / 2;
  g.add(hub);
  const sticker = new THREE.Mesh(new THREE.RingGeometry(hole * 1.3, radius * 0.8, 72),
    P({ map: canvasTexture(512, 512, labelArt(label, band)), roughness: 0.55, clearcoat: 0.2, polygonOffset: true, polygonOffsetFactor: -2 }));
  sticker.rotation.x = -Math.PI / 2;
  sticker.position.y = height + 0.00005;
  g.add(sticker);
  return g;
}

// ------------------------------------------------------------------ caliper
// A small digital caliper lying on the mat: stainless beam with an engraved millimetre scale, fixed
// jaw, and the slider with its jaw, LCD and buttons. Origin at the beam's centre, x along the beam.
function scaleArt(g, w, h) {
  g.fillStyle = "#c9ccd1"; g.fillRect(0, 0, w, h);
  const s = w / 0.12;
  g.fillStyle = "#2a2c30";
  g.font = `600 ${0.0024 * s}px "Martian Mono", ui-monospace, monospace`;
  g.textBaseline = "top";
  for (let mm = 0; mm <= 100; mm++) {
    const x = (0.012 + mm * 0.001) * s, len = mm % 10 === 0 ? 0.0045 : mm % 5 === 0 ? 0.0032 : 0.002;
    g.fillRect(x - 0.6, 0, 1.2, len * s);
    if (mm % 10 === 0) g.fillText(String(mm / 10), x + 2, 0.005 * s);
  }
}

function caliperLcdArt(g, w, h) {
  g.fillStyle = "#a9b39f"; g.fillRect(0, 0, w, h);
  g.fillStyle = "#1d2219"; g.textBaseline = "middle"; g.textAlign = "right";
  g.font = `700 ${h * 0.62}px Doto, ui-monospace, monospace`;
  g.fillText("25.40", w * 0.84, h * 0.56);
  g.font = `600 ${h * 0.2}px "Martian Mono", ui-monospace, monospace`;
  g.fillText("mm", w * 0.97, h * 0.7);
  g.textAlign = "left"; g.fillText("ABS", w * 0.04, h * 0.18);
}

export function caliper() {
  const c = new THREE.Group();
  const steel = P({ color: 0x8e9298, metalness: 1, roughness: 0.48 });       // satin stainless
  const add = (geometry, material, x, y, z) => {
    const m = new THREE.Mesh(geometry, material);
    m.position.set(x, y, z);
    m.castShadow = m.receiveShadow = true;
    c.add(m);
    return m;
  };
  const L = 0.12, T = 0.0028, Wb = 0.014;
  // the beam, its scale printed on the top face only
  const scale = P({ color: 0xa4a8ae, metalness: 1, roughness: 0.48, map: canvasTexture(1600, 190, scaleArt) });
  add(new THREE.BoxGeometry(L, T, Wb), [steel, steel, scale, steel, steel, steel], 0, T / 2, 0);
  // fixed jaw at the left end: a long measuring jaw toward +z, a short one toward -z
  add(new THREE.BoxGeometry(0.008, T, 0.034), steel, -L / 2 + 0.004, T / 2, 0.022);
  add(new THREE.BoxGeometry(0.005, T, 0.012), steel, -L / 2 + 0.0025, T / 2, -0.013);
  // the slider, 25.4 mm along: its jaws, the black electronics housing, LCD, two buttons, thumb roller
  const sx = -L / 2 + 0.008 + 0.0254 + 0.012;
  add(new THREE.BoxGeometry(0.024, T * 1.2, Wb + 0.003), steel, sx, T * 0.6, 0);
  add(new THREE.BoxGeometry(0.008, T, 0.03), steel, sx - 0.008, T / 2, 0.02);
  add(new THREE.BoxGeometry(0.005, T, 0.012), steel, sx - 0.0095, T / 2, -0.013);
  const housing = P({ color: 0x17181b, roughness: 0.42, clearcoat: 0.4, clearcoatRoughness: 0.3 });
  add(new RoundedBoxGeometry(0.034, 0.006, 0.026, 3, 0.0018), housing, sx + 0.004, T + 0.003, -0.004);
  add(new THREE.PlaneGeometry(0.019, 0.0085), P({ map: canvasTexture(512, 230, caliperLcdArt), roughness: 0.18, clearcoat: 1 }), sx + 0.004, T + 0.0061, -0.006)
    .rotation.x = -Math.PI / 2;
  for (const [i, color] of [[0, 0xd23a2a], [1, 0xe0e0dc]]) {
    add(new THREE.CylinderGeometry(0.0022, 0.0022, 0.0012, 20), P({ color, roughness: 0.4 }), sx - 0.0065 + i * 0.006, T + 0.0062, 0.0055);
  }
  const roller = add(new THREE.CylinderGeometry(0.003, 0.003, 0.004, 20), P({ color: 0x9a9da3, metalness: 1, roughness: 0.4, bumpMap: knurl, bumpScale: 1 }),
    sx + 0.012, T + 0.002, 0.0095);
  roller.rotation.x = Math.PI / 2;
  return c;
}
