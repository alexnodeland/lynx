// SPDX-License-Identifier: MIT
import { resolve } from "node:path";
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",                       // works on GitHub Pages under /<repo>/
  build: {
    target: "es2022",
    chunkSizeWarningLimit: 1600,
    rollupOptions: { input: {
        main: resolve(__dirname, "index.html"),
        build: resolve(__dirname, "build.html"),
        flash: resolve(__dirname, "flash.html"),
      } },
  },
});
