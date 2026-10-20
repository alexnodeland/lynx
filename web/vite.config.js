// SPDX-License-Identifier: MIT
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",                       // works on GitHub Pages under /<repo>/
  build: { target: "es2022", chunkSizeWarningLimit: 1600 },
});
