import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { expect, test } from '@playwright/test';

/**
 * `npm run export` on the drone tour, then the one-file page opened straight from disk
 * (file://, no server): the landing card, the throttle, a dive and a refresh on the new level.
 */
let out = '';
test.beforeAll(() => {
  out = mkdtempSync(join(tmpdir(), '3disle-export-'));
  execFileSync(
    process.execPath,
    [
      '--experimental-strip-types',
      '--no-warnings=ExperimentalWarning',
      'scripts/export-page.ts',
      'models/drone/tour.json',
      '--single',
      '--name',
      'quad',
      '--out',
      out,
    ],
    { stdio: 'pipe' },
  );
});
test.afterAll(() => rmSync(out, { recursive: true, force: true }));

test('the exported one-file page opens from disk, dives and comes back after a refresh', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
  page.on('requestfailed', (r) => errors.push(`failed: ${r.url()}`));

  await page.goto(pathToFileURL(join(out, 'quad.html')).href);
  await expect(page).toHaveTitle(/Inside a 5-inch FPV quad/, { timeout: 90_000 });
  await expect(page.locator('#landing')).toBeVisible({ timeout: 90_000 });
  await expect(page.locator('#landing h1')).toHaveText('Inside a\n5-inch FPV quad', { useInnerText: true });
  // An exported page has no model list and no links to the rest of the app.
  await expect(page.locator('#library')).toBeHidden();
  await expect(page.locator('.bar .open')).toHaveCount(2);
  for (const link of await page.locator('.bar .open').all()) await expect(link).toBeHidden();

  await page.locator('#explore-own').click();
  await expect(page.locator('#hud .ttl')).toHaveText('5-inch FPV quad');
  await page.locator('input[data-control="throttle"]').fill('100');
  await expect(page.locator('#controls output')).toHaveText('100 %');

  await page.locator('#next').click();
  await expect(page.locator('#hud .ttl')).toHaveText('SpeedyBee F405 V5 flight controller', { timeout: 60_000 });
  await expect(page.locator('#counter')).toHaveText('02 / 03');
  await expect(page.locator('#controls output')).toHaveText('100 %'); // the throttle carries into the dive
  expect(new URL(page.url()).searchParams.get('path')).toBe('drone/fc.json');

  await page.reload();
  await expect(page.locator('#hud .ttl')).toHaveText('SpeedyBee F405 V5 flight controller', { timeout: 90_000 });
  await expect(page.locator('#crumbs')).toContainText('Inside a 5-inch FPV quad');
  expect(errors).toEqual([]);
});
