import { defineConfig } from 'vite';

/**
 * The viewer alone, as one script and one stylesheet, for `npm run export` (scripts/export-page.ts).
 * No public/ copy: the export takes only the data files its teardown reaches.
 */
export default defineConfig({
  base: './',
  publicDir: false,
  build: {
    outDir: 'dist-export',
    emptyOutDir: true,
    target: 'es2022',
    modulePreload: false,
    chunkSizeWarningLimit: 1500,
    rollupOptions: {
      input: 'viewer.html',
      output: { codeSplitting: false },
    },
  },
});
