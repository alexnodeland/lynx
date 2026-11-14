// SPDX-License-Identifier: MIT
// The silicone repair mat the product stands on, built procedurally: a thin floor, a top layer with
// recessed trays and screw cups cut through it, soft bevelled edges, and white print on top.
import * as THREE from "three";

const W = 0.52, D = 0.36;                  // metres, x by z
const FLOOR = 0.001, TOP = 0.0023;         // layer thicknesses: the trays are TOP deep
const BLUE = "#1f5fa6";

function roundedRect(shape, x, y, w, h, r) {
  shape.moveTo(x + r, y);
  shape.lineTo(x + w - r, y);
  shape.quadraticCurveTo(x + w, y, x + w, y + r);
  shape.lineTo(x + w, y + h - r);
  shape.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  shape.lineTo(x + r, y + h);
  shape.quadraticCurveTo(x, y + h, x, y + h - r);
  shape.lineTo(x, y + r);
  shape.quadraticCurveTo(x, y, x + r, y);
  return shape;
}

// trays and cups in shape space (x right, y toward the back edge; the mat is centred on 0,0)
const TRAYS = [
  [-0.235, 0.115, 0.088, 0.052], [-0.138, 0.115, 0.088, 0.052], [-0.041, 0.115, 0.13, 0.052],
  [0.098, 0.115, 0.06, 0.052], [0.167, 0.115, 0.06, 0.052],
];
const CUPS = [[-0.215, 0.07], [-0.215, 0.035], [-0.215, 0.0]];
const CUP_R = 0.013;

function printTexture(anisotropy) {
  const px = 2400, c = document.createElement("canvas");
  c.width = px; c.height = Math.round((px * D) / W);
  const g = c.getContext("2d"), s = px / W;                      // pixels per metre
  const X = (x) => (x + W / 2) * s, Y = (y) => (D / 2 - y) * s;   // shape space -> canvas
  g.fillStyle = BLUE; g.fillRect(0, 0, c.width, c.height);
  // a little unevenness in the silicone
  for (let i = 0; i < 9000; i++) {
    g.fillStyle = `rgba(${Math.random() < 0.5 ? "0,10,40" : "200,225,255"},${Math.random() * 0.03})`;
    g.fillRect(Math.random() * c.width, Math.random() * c.height, 4, 4);
  }
  const ink = (a) => `rgba(222,236,250,${a})`;
  g.lineCap = "round";
  // outlines printed around the trays and cups
  g.strokeStyle = ink(0.5); g.lineWidth = 3;
  for (const [x, y, w, h] of TRAYS) {
    g.beginPath(); g.roundRect(X(x) - 9, Y(y + h) - 9, w * s + 18, h * s + 18, 0.008 * s); g.stroke();
  }
  for (const [x, y] of CUPS) { g.beginPath(); g.arc(X(x), Y(y), CUP_R * s + 9, 0, Math.PI * 2); g.stroke(); }
  // a 10 mm work grid on the right, a heavier line every 50 mm
  const gx0 = 0.09, gx1 = 0.245, gy0 = -0.15, gy1 = 0.09;
  for (let x = gx0; x <= gx1 + 1e-6; x += 0.01) {
    const major = Math.round((x - gx0) / 0.01) % 5 === 0;
    g.strokeStyle = ink(major ? 0.42 : 0.2); g.lineWidth = major ? 2.4 : 1.4;
    g.beginPath(); g.moveTo(X(x), Y(gy0)); g.lineTo(X(x), Y(gy1)); g.stroke();
  }
  for (let y = gy0; y <= gy1 + 1e-6; y += 0.01) {
    const major = Math.round((y - gy0) / 0.01) % 5 === 0;
    g.strokeStyle = ink(major ? 0.42 : 0.2); g.lineWidth = major ? 2.4 : 1.4;
    g.beginPath(); g.moveTo(X(gx0), Y(y)); g.lineTo(X(gx1), Y(y)); g.stroke();
  }
  // a millimetre rule along the front edge
  g.strokeStyle = ink(0.6);
  for (let i = 0; i <= 400; i++) {
    const x = -0.2 + i * 0.001, len = i % 10 === 0 ? 0.008 : i % 5 === 0 ? 0.005 : 0.0028;
    g.lineWidth = i % 10 === 0 ? 2.4 : 1.4;
    g.beginPath(); g.moveTo(X(x), Y(-D / 2 + 0.012)); g.lineTo(X(x), Y(-D / 2 + 0.012 + len)); g.stroke();
  }
  g.fillStyle = ink(0.62); g.textBaseline = "middle";
  g.font = `600 ${0.0048 * s}px "Martian Mono", ui-monospace, monospace`;
  for (let cm = 0; cm <= 40; cm += 5) g.fillText(String(cm), X(-0.2 + cm * 0.01) + 6, Y(-D / 2 + 0.012 + 0.0115));
  g.font = `800 ${0.0075 * s}px Archivo, system-ui, sans-serif`;
  g.fillText("lynx", X(-0.235), Y(-0.04));
  g.font = `500 ${0.0042 * s}px "Martian Mono", ui-monospace, monospace`;
  g.fillText("SILICONE WORK MAT · 500 °C · ESD SAFE", X(-0.235), Y(-0.052));
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = anisotropy;
  // ExtrudeGeometry's top face takes the shape's x/y (metres) as uv: map the mat's extent onto 0..1
  t.repeat.set(1 / W, 1 / D);
  t.offset.set(0.5, 0.5);
  return t;
}

// moulded silicone: a fine stipple (height) and a little wear (roughness), tiled across the mat
function stipple() {
  const c = document.createElement("canvas");
  c.width = c.height = 256;
  const g = c.getContext("2d"), img = g.createImageData(256, 256);
  for (let i = 0; i < img.data.length; i += 4) {
    const v = 128 + (Math.random() - 0.5) * 70;
    img.data[i] = img.data[i + 1] = img.data[i + 2] = v; img.data[i + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  g.filter = "blur(1px)"; g.drawImage(c, 0, 0);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  // the top face's uv is the shape's x/y in metres: about 3 mm per tile
  t.repeat.set(330, 330);
  return t;
}

function wear() {
  const c = document.createElement("canvas");
  c.width = c.height = 512;
  const g = c.getContext("2d");
  g.fillStyle = "rgb(150,150,150)"; g.fillRect(0, 0, 512, 512);
  for (let i = 0; i < 260; i++) {                    // smudges: smoother, shinier patches
    const x = Math.random() * 512, y = Math.random() * 512, r = 6 + Math.random() * 40;
    const grad = g.createRadialGradient(x, y, 0, x, y, r);
    grad.addColorStop(0, `rgba(120,120,120,${0.15 + Math.random() * 0.2})`); grad.addColorStop(1, "rgba(120,120,120,0)");
    g.fillStyle = grad; g.fillRect(x - r, y - r, r * 2, r * 2);
  }
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(4.5, 4.5);
  return t;
}

export function siliconeMat({ anisotropy = 8 } = {}) {
  const material = new THREE.MeshPhysicalMaterial({
    map: printTexture(anisotropy), roughness: 0.7, roughnessMap: wear(), bumpMap: stipple(), bumpScale: 0.45,
    sheen: 0.5, sheenRoughness: 0.45, sheenColor: 0x7fb2ee,
  });
  const outline = () => roundedRect(new THREE.Shape(), -W / 2, -D / 2, W, D, 0.014);
  const floor = new THREE.ExtrudeGeometry(outline(), { depth: FLOOR, bevelEnabled: false, curveSegments: 8 });
  const topShape = outline();
  for (const [x, y, w, h] of TRAYS) topShape.holes.push(roundedRect(new THREE.Path(), x, y, w, h, 0.007));
  for (const [x, y] of CUPS) topShape.holes.push(new THREE.Path().absarc(x, y, CUP_R, 0, Math.PI * 2, true));
  const top = new THREE.ExtrudeGeometry(topShape, {
    depth: TOP - 0.0006, bevelEnabled: true, bevelThickness: 0.0006, bevelSize: 0.0008, bevelSegments: 3, curveSegments: 10,
  });
  top.translate(0, 0, FLOOR);
  const mat = new THREE.Group();
  for (const g of [floor, top]) {
    const m = new THREE.Mesh(g, material);
    m.receiveShadow = true;
    mat.add(m);
  }
  mat.rotation.x = -Math.PI / 2;            // shape y -> world -z (the back edge)
  mat.name = "SiliconeMat";
  mat.add(screwsInCups());
  return mat;
}

export const MAT_THICKNESS = FLOOR + TOP;

// A few M2 x 8 screws (the build's one screw type) dropped in the screw cups, in the mat's frame
// (shape x/y, z up from the cup floor).
function screwsInCups() {
  const head = new THREE.CylinderGeometry(0.00175, 0.00175, 0.0013, 18).rotateX(Math.PI / 2).translate(0, 0, 0.00065);
  const shank = new THREE.CylinderGeometry(0.0008, 0.0007, 0.0067, 10).rotateX(Math.PI / 2).translate(0, 0, 0.0013 + 0.00335);
  const steel = new THREE.MeshPhysicalMaterial({ color: 0x2b2c30, metalness: 1, roughness: 0.32 });   // black-oxide
  const heads = new THREE.InstancedMesh(head, steel, 9), shanks = new THREE.InstancedMesh(shank, steel, 9);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), one = new THREE.Vector3(1, 1, 1);
  let i = 0;
  for (const [cx, cy] of CUPS) for (let k = 0; k < 3; k++) {
    // lying on their sides in the cup, at random angles
    const a = Math.random() * Math.PI * 2, d = Math.random() * CUP_R * 0.45;
    q.setFromEuler(new THREE.Euler(0, Math.PI / 2, Math.random() * Math.PI * 2));
    m.compose(new THREE.Vector3(cx + Math.cos(a) * d, cy + Math.sin(a) * d, FLOOR + 0.00175), q, one);
    heads.setMatrixAt(i, m); shanks.setMatrixAt(i, m); i++;
  }
  const screws = new THREE.Group();
  for (const s of [heads, shanks]) { s.castShadow = s.receiveShadow = true; screws.add(s); }
  return screws;
}

// Gull-wing legs along the long sides of a chip body (the set's chips are plain boxes): 1.27 mm pitch,
// one InstancedMesh per chip, in the chip's own frame.
export function addLegs(chip, material) {
  chip.geometry.computeBoundingBox();
  const size = chip.geometry.boundingBox.getSize(new THREE.Vector3());
  const alongX = size.x >= size.z, length = alongX ? size.x : size.z, width = alongX ? size.z : size.x;
  const count = Math.max(2, Math.floor(length / 0.00127));
  const legs = new THREE.InstancedMesh(new THREE.BoxGeometry(0.00042, 0.00022, 0.0011), material, count * 2);
  const m = new THREE.Matrix4(), turn = new THREE.Matrix4().makeRotationY(alongX ? 0 : Math.PI / 2);
  let i = 0;
  for (const side of [-1, 1]) for (let k = 0; k < count; k++) {
    const a = (k - (count - 1) / 2) * 0.00127, b = side * (width / 2 + 0.0003), y = -size.y / 2 + 0.00011;
    m.makeTranslation(alongX ? a : b, y, alongX ? b : a).multiply(turn);
    legs.setMatrixAt(i++, m);
  }
  legs.castShadow = true;
  chip.add(legs);
}
