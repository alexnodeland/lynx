// SPDX-License-Identifier: MIT
// Rebuild the Blender look in three.js. glTF carries base colour / roughness / metalness, but not
// the procedural layers (print lines, braid, mat grid) — those are regenerated here as canvas maps.
import * as THREE from "three";

function canvasTex(w, h, draw, { repeat = [1, 1], srgb = false } = {}) {
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

// FDM layer lines as a height map -> used as a bump map (0.2 mm layers on a ~30 mm part)
const layerBump = canvasTex(8, 512, (g, w, h) => {
  for (let y = 0; y < h; y++) {
    const v = 128 + 70 * Math.sin((y / h) * Math.PI * 2 * 64) + (Math.random() - 0.5) * 14;
    g.fillStyle = `rgb(${v},${v},${v})`; g.fillRect(0, y, w, 1);
  }
});

const gridMap = canvasTex(1024, 1024, (g, w, h) => {
  g.fillStyle = "#0b2140"; g.fillRect(0, 0, w, h);
  g.strokeStyle = "rgba(120,160,205,0.55)"; g.lineWidth = 2;
  for (let i = 0; i <= 16; i++) {
    const p = (i / 16) * w;
    g.beginPath(); g.moveTo(p, 0); g.lineTo(p, h); g.stroke();
    g.beginPath(); g.moveTo(0, p); g.lineTo(w, p); g.stroke();
  }
  for (let i = 0; i < 9000; i++) { g.fillStyle = `rgba(255,255,255,${Math.random() * 0.025})`; g.fillRect(Math.random() * w, Math.random() * h, 2, 2); }
}, { repeat: [3, 3], srgb: true });

const braidBump = canvasTex(64, 64, (g, w, h) => {
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const a = Math.sin(((x + y) / w) * Math.PI * 8), b = Math.sin(((x - y) / w) * Math.PI * 8);
    const v = 128 + 110 * Math.max(a, b);
    g.fillStyle = `rgb(${v},${v},${v})`; g.fillRect(x, y, 1, 1);
  }
}, { repeat: [80, 2] });

const P = (o) => new THREE.MeshPhysicalMaterial(o);
const LIBRARY = {
  PLA_offwhite: () => P({ color: 0xc8c3b8, roughness: 0.5, sheen: 0.15, sheenRoughness: 0.8, bumpMap: layerBump, bumpScale: 0.6 }),
  PLA_charcoal: () => P({ color: 0x1c1d20, roughness: 0.55, sheen: 0.25, sheenColor: 0x444444, bumpMap: layerBump, bumpScale: 0.6 }),
  PETG_frost: () => P({ color: 0xeef0f4, roughness: 0.34, transmission: 1, thickness: 1.5, ior: 1.57, bumpMap: layerBump, bumpScale: 0.4 }),
  PETG_clear: () => P({ color: 0xf6fbff, roughness: 0.06, transmission: 1, thickness: 1.0, ior: 1.57 }),
  servo_black: () => P({ color: 0x0d0d0f, roughness: 0.45, clearcoat: 0.3, clearcoatRoughness: 0.5 }),
  horn_nylon: () => P({ color: 0xdedbd4, roughness: 0.42 }),
  pcb_soldermask: () => P({ color: 0x0b3a1c, roughness: 0.3, clearcoat: 0.8, clearcoatRoughness: 0.15 }),
  pcb_black: () => P({ color: 0x08080a, roughness: 0.32, clearcoat: 0.8, clearcoatRoughness: 0.15 }),
  enig_gold: () => P({ color: 0xffc66b, metalness: 1, roughness: 0.22 }),
  tin_shield: () => P({ color: 0xd2d3d6, metalness: 1, roughness: 0.2 }),
  nickel: () => P({ color: 0xc9c4bb, metalness: 1, roughness: 0.16 }),
  alu_brushed: () => P({ color: 0xdcdee2, metalness: 1, roughness: 0.3 }),
  lipo_foil: () => P({ color: 0xb9bcc3, metalness: 1, roughness: 0.25 }),
  lcp_black: () => P({ color: 0x0a0a0b, roughness: 0.4 }),
  connector_beige: () => P({ color: 0xd2bf93, roughness: 0.45 }),
  lens_glass: () => P({ color: 0xe6f0ff, roughness: 0.02, transmission: 1, thickness: 0.5, ior: 1.52, clearcoat: 1, iridescence: 0.4 }),
  cable_rubber: () => P({ color: 0x0c0c0d, roughness: 0.68, sheen: 0.4, sheenColor: 0x222222 }),
  antenna_flex: () => P({ color: 0xcc8c22, roughness: 0.35, clearcoat: 0.5 }),
  microsd: () => P({ color: 0x0b0b0e, roughness: 0.35, clearcoat: 0.4 }),
  vent_mesh: () => P({ color: 0x9a9a9e, metalness: 1, roughness: 0.35 }),
  led: () => P({ color: 0xe8f5ee, roughness: 0.15, emissive: 0x40ff88, emissiveIntensity: 0 }),
  // the bench
  esd_mat_grid: () => P({ map: gridMap, roughness: 0.78 }),
  desk_oak_scan: (m) => { m.roughness = 0.55; return m; },     // keep the scanned oak textures from the glb
  wall: () => P({ color: 0x0c0c0e, roughness: 0.95 }),
  station_blue: () => P({ color: 0x2b3a99, roughness: 0.45, clearcoat: 0.2 }),
  station_yellow: () => P({ color: 0xd99f1c, roughness: 0.5 }),
  spool_white: () => P({ color: 0xe2e2df, roughness: 0.5 }),
  spool_green: () => P({ color: 0x1f8c47, roughness: 0.45 }),
  copper: () => P({ color: 0xf2a37a, metalness: 1, roughness: 0.3 }),
  solder: () => P({ color: 0xcfd0d4, metalness: 1, roughness: 0.32 }),
};

export const cableMaterial = () => P({ color: 0x141416, roughness: 0.62, sheen: 0.5, sheenColor: 0x333333, bumpMap: braidBump, bumpScale: 1.2 });
export const powerCableMaterial = () => LIBRARY.cable_rubber();

export function restyle(root) {
  const cache = new Map();
  root.traverse((o) => {
    if (!o.isMesh) return;
    o.castShadow = o.receiveShadow = true;
    const name = (o.material && o.material.name) || "";
    const make = LIBRARY[name];
    if (!make) return;
    if (!cache.has(name)) {
      const m = make(o.material);
      m.name = name;
      cache.set(name, m);
    }
    o.material = cache.get(name);
  });
  return cache;
}
