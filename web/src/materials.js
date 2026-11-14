// SPDX-License-Identifier: MIT
// The product and bench finishes, looked up by glTF material name. The product is dressed as the
// finished-hardware renders show it: satin white head, black anodised yoke and shroud, gunmetal base,
// a coated lens. The CAD meshes carry no UVs, so its finishes are material response, not textures.
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

const braidBump = canvasTex(64, 64, (g, w, h) => {
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const a = Math.sin(((x + y) / w) * Math.PI * 8), b = Math.sin(((x - y) / w) * Math.PI * 8);
    const v = 128 + 110 * Math.max(a, b);
    g.fillStyle = `rgb(${v},${v},${v})`; g.fillRect(x, y, 1, 1);
  }
}, { repeat: [80, 2] });

const P = (o) => new THREE.MeshPhysicalMaterial(o);
const satinWhite = { color: 0xe8e9e8, roughness: 0.48, clearcoat: 0.12, clearcoatRoughness: 0.5 };
const LIBRARY = {
  PLA_offwhite: () => P(satinWhite),
  PLA_charcoal: () => P({ color: 0x2c2d31, metalness: 0.6, roughness: 0.34, clearcoat: 0.35, clearcoatRoughness: 0.3 }),   // black anodised
  anodised_gunmetal: () => P({ color: 0x83878e, metalness: 0.3, roughness: 0.45, clearcoat: 0.3, clearcoatRoughness: 0.35 }),
  PETG_frost: () => P({ ...satinWhite, color: 0xececea, roughness: 0.42 }),                 // the lid, closed
  PETG_clear: () => P({ color: 0xeef4f1, roughness: 0.2, emissive: 0x40ff88, emissiveIntensity: 0 }),   // the light pipe glows with the LED
  servo_black: () => P({ color: 0x111113, roughness: 0.42, clearcoat: 0.3, clearcoatRoughness: 0.4 }),
  horn_nylon: () => P({ color: 0xe2dfd8, roughness: 0.42 }),
  pcb_soldermask: () => P({ color: 0x0b3a1c, roughness: 0.3, clearcoat: 0.8, clearcoatRoughness: 0.15 }),
  pcb_black: () => P({ color: 0x08080a, roughness: 0.32, clearcoat: 0.8, clearcoatRoughness: 0.15 }),
  enig_gold: () => P({ color: 0xffc66b, metalness: 1, roughness: 0.22 }),
  tin_shield: () => P({ color: 0xd2d3d6, metalness: 1, roughness: 0.2 }),
  nickel: () => P({ color: 0xd6d3cd, metalness: 1, roughness: 0.12 }),
  alu_brushed: () => P({ color: 0xdcdee2, metalness: 1, roughness: 0.3 }),
  lipo_foil: () => P({ color: 0xb9bcc3, metalness: 1, roughness: 0.25 }),
  lcp_black: () => P({ color: 0x0a0a0b, roughness: 0.4 }),
  connector_beige: () => P({ color: 0xd2bf93, roughness: 0.45 }),
  // multi-coated lens: near black, mirror smooth, blue-violet thin-film reflections
  lens_glass: () => P({ color: 0x04060c, roughness: 0.03, clearcoat: 1, clearcoatRoughness: 0.02, iridescence: 1,
    iridescenceIOR: 1.7, iridescenceThicknessMinimum: 280, iridescenceThicknessMaximum: 420 }),
  cable_rubber: () => P({ color: 0x0e0e10, roughness: 0.55, sheen: 0.4, sheenColor: 0x2a2a2a }),
  antenna_flex: () => P({ color: 0xcc8c22, roughness: 0.35, clearcoat: 0.5 }),
  microsd: () => P({ color: 0x0b0b0e, roughness: 0.35, clearcoat: 0.4 }),
  vent_mesh: () => P({ color: 0x55585e, metalness: 1, roughness: 0.4 }),
  led: () => P({ color: 0xe8f5ee, roughness: 0.15, emissive: 0x40ff88, emissiveIntensity: 0 }),
  // the bench
  desk_oak_scan: (m) => { m.roughness = 0.72; m.color.setScalar(0.7); m.specularIntensity = 0.15; return m; },   // the glb's scanned oak, oiled not lacquered
  station_blue: () => P({ color: 0x3e52b8, roughness: 0.4, clearcoat: 0.35, clearcoatRoughness: 0.3 }),
  station_yellow: () => P({ color: 0xe0a51c, roughness: 0.45, clearcoat: 0.2 }),
  spool_white: () => P({ color: 0xe6e6e2, roughness: 0.45 }),
  spool_green: () => P({ color: 0x1f8c47, roughness: 0.4 }),
  copper: () => P({ color: 0xf2a37a, metalness: 1, roughness: 0.26 }),
  solder: () => P({ color: 0xd4d5d9, metalness: 1, roughness: 0.24 }),
};
// parts that share a glTF material with others but get their own finish
const BY_NODE = { PanBase: "anodised_gunmetal", PanCap: "anodised_gunmetal" };

// braided sleeving (the cables carry a tube UV, so the weave maps along them)
export const cableMaterial = () => P({ color: 0x18181a, roughness: 0.6, sheen: 0.6, sheenColor: 0x3a3a3a, bumpMap: braidBump, bumpScale: 1.4 });
export const powerCableMaterial = () => LIBRARY.cable_rubber();

export function restyle(root) {
  const cache = new Map();
  root.traverse((o) => {
    if (!o.isMesh) return;
    o.castShadow = o.receiveShadow = true;
    const name = BY_NODE[o.name] ?? o.material?.name ?? "";
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
