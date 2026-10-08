import { defineConfig } from '@playwright/test';

/**
 * Smoke tests run against the production build (vite preview) in Chromium with SwiftShader,
 * so they work in CI without a GPU. They check function, not frame rate.
 */
// PW_PORT lets two runs (e.g. visual and e2e) share a machine.
const port = Number(process.env.PW_PORT ?? 4173);

export default defineConfig({
  testDir: 'tests/e2e',
  timeout: 240_000,
  expect: { timeout: 60_000 },
  fullyParallel: false,
  workers: 1,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? [['list'], ['github']] : 'list',
  use: {
    baseURL: `http://localhost:${port}`,
    viewport: { width: 960, height: 540 },
    launchOptions: {
      // PW_CHROMIUM runs the tests in an installed Chromium instead of Playwright's download.
      executablePath: process.env.PW_CHROMIUM || undefined,
      args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
    },
  },
  webServer: {
    command: `npx vite preview --port ${port} --strictPort`,
    url: `http://localhost:${port}`,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
});
