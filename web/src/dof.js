// SPDX-License-Identifier: MIT
// Depth of field as a single gather pass over an existing depth texture (the AO pass's), after
// Dennis Gustafsson's "Bokeh depth of field in a single pass". Each sample only counts as far as its
// own blur reaches, so a sharp subject doesn't smear a halo over the blurred background behind it,
// and samples behind the pixel can't spread wider than twice its own blur.
import { Vector2 } from "three";
import { ShaderPass } from "three/examples/jsm/postprocessing/ShaderPass.js";

const TAPS = 40;

const DepthOfFieldShader = {
  name: "DepthOfFieldShader",
  defines: { TAPS },
  uniforms: {
    tColor: { value: null },
    tDepth: { value: null },
    resolution: { value: new Vector2(1, 1) },
    focus: { value: 1 },        // focus distance, metres
    aperture: { value: 0.02 },  // blur per metre out of focus, as a fraction of the frame height
    maxblur: { value: 0.01 },   // blur cap, as a fraction of the frame height
    nearClip: { value: 0.1 },
    farClip: { value: 100 },
  },
  vertexShader: /* glsl */ `
    varying vec2 vUv;
    void main() {
      vUv = uv;
      gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
    }`,
  fragmentShader: /* glsl */ `
    #include <packing>
    uniform sampler2D tColor;
    uniform sampler2D tDepth;
    uniform vec2 resolution;
    uniform float focus, aperture, maxblur, nearClip, farClip;
    varying vec2 vUv;

    const float GOLDEN_ANGLE = 2.39996323;

    float viewDepth(vec2 uv) {
      return -perspectiveDepthToViewZ(texture2D(tDepth, uv).x, nearClip, farClip);
    }
    // circle of confusion radius in pixels
    float blurSize(float depth) {
      return min(abs(depth - focus) * aperture, maxblur) * resolution.y;
    }

    void main() {
      float centerDepth = viewDepth(vUv);
      float centerSize = blurSize(centerDepth);
      vec3 color = texture2D(tColor, vUv).rgb;
      float total = 1.0;
      float maxRadius = maxblur * resolution.y;
      vec2 texel = 1.0 / resolution;
      for (int i = 1; i < TAPS; i++) {
        float radius = maxRadius * sqrt(float(i) / float(TAPS));   // even coverage of the disc
        float angle = float(i) * GOLDEN_ANGLE;
        vec2 uv = vUv + vec2(cos(angle), sin(angle)) * texel * radius;
        vec3 sampleColor = texture2D(tColor, uv).rgb;
        float sampleDepth = viewDepth(uv);
        float sampleSize = blurSize(sampleDepth);
        if (sampleDepth > centerDepth) sampleSize = min(sampleSize, centerSize * 2.0);
        float m = smoothstep(radius - 1.0, radius + 1.0, sampleSize);
        color += mix(color / total, sampleColor, m);
        total += 1.0;
      }
      gl_FragColor = vec4(color / total, 1.0);
    }`,
};

export class DepthOfFieldPass extends ShaderPass {
  constructor(camera, depthTexture, { focus, aperture, maxblur }) {
    super(DepthOfFieldShader, "tColor");
    const u = this.uniforms;
    u.tDepth.value = depthTexture;
    u.focus.value = focus;
    u.aperture.value = aperture;
    u.maxblur.value = maxblur;
    u.nearClip.value = camera.near;
    u.farClip.value = camera.far;
  }

  setSize(width, height) {
    this.uniforms.resolution.value.set(width, height);
  }
}
