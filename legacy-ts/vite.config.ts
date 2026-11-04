import { defineConfig } from 'vite';
import { projectsPlugin } from './scripts/projects-plugin.ts';

export default defineConfig({
  base: './',
  // Local only: open it from another device through an SSH tunnel (see INSTALL.md).
  server: { host: '127.0.0.1' },
  plugins: [projectsPlugin()],
  build: {
    target: 'es2022',
    chunkSizeWarningLimit: 1500,
    rollupOptions: {
      input: {
        main: 'index.html',
        viewer: 'viewer.html',
        editor: 'editor.html',
      },
    },
  },
});
