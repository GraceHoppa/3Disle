import { expect, test, type Page } from '@playwright/test';

/** Wait until the viewer has finished loading whatever it was asked to show. */
async function loaded(page: Page) {
  await expect(page.locator('#title')).not.toHaveText('Loading…', { timeout: 90_000 });
  await expect(page.locator('#status')).not.toHaveText('Loading…', { timeout: 90_000 });
}

function collectErrors(page: Page) {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
  return errors;
}

test('lists the models and opens the drone tour first', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html');
  await loaded(page);
  await expect(page.locator('#builtin button')).toHaveCount(8);
  await expect(page.locator('#counter')).toHaveText('01 / 02');
  await expect(page.locator('#hud .ttl')).toHaveText('SpeedyBee F405 V5 flight controller');
  await expect(page.locator('#xray')).toBeHidden();
  await expect(page.locator('.cut')).toBeHidden();
  expect(errors).toEqual([]);
});

test('a tour opens on its landing card; the last ▶ opens the finale', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html?model=rtx5070ti/tour.json');
  await expect(page.locator('#landing')).toBeVisible({ timeout: 90_000 });
  await expect(page.locator('#landing h1')).toHaveText('Inside the\nGeForce RTX 5070 Ti', { useInnerText: true });
  await expect(page.locator('#landing .acts li')).toHaveText(['IThe board', 'IIThe silicon']);
  await expect(page.locator('#ruler .ruler-level')).toHaveCount(4);
  await page.locator('#explore-own').click();
  await expect(page.locator('#landing')).toBeHidden();
  await expect(page.locator('#ruler .ruler-level[aria-current="true"]')).toHaveAttribute('title', '30 cm');
  await expect(page.locator('#ruler .ruler-mark').first()).toHaveText('a fingernail'); // the RTX tour's ruler starts at 1 m
  expect(errors).toEqual([]);
});

test('arrows walk any model down its Dive-in links and back', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html?model=rtx5070ti/card.json');
  await loaded(page);
  await expect(page.locator('#counter')).toHaveText('Level 01');
  await expect(page.locator('#prev')).toBeDisabled();
  await page.locator('#next').click();
  await expect(page.locator('#counter')).toHaveText('Level 02', { timeout: 60_000 });
  await expect(page.locator('#title')).toHaveText('Main PCB', { timeout: 60_000 });
  await expect(page.locator('#flow')).toBeVisible(); // the PCB level carries the data buses
  await page.locator('#prev').click();
  await expect(page.locator('#counter')).toHaveText('Level 01', { timeout: 60_000 });
  expect(errors).toEqual([]);
});

test('hovering a part shows its name and info', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/card.json');
  await loaded(page);
  const box = (await page.locator('#gl').boundingBox())!;
  await page.mouse.move(box.width / 2, box.height / 2);
  await page.mouse.move(box.width / 2 + 4, box.height / 2 + 4);
  await expect(page.locator('#tip')).toBeVisible();
  await expect(page.locator('#tip strong')).not.toBeEmpty();
});

// Cut and X-Ray are hidden for now (viewer.html); these come back with them.
test.skip('section cut: toggles, picks an axis and moves', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html?model=rtx5070ti/package.json');
  await loaded(page);
  await expect(page.locator('#cut-pos')).toBeHidden();
  await page.locator('#cut').click();
  await expect(page.locator('#cut')).toHaveAttribute('aria-pressed', 'true');
  await page.locator('[data-axis="z"]').click();
  await expect(page.locator('[data-axis="z"]')).toHaveAttribute('aria-pressed', 'true');
  await page.locator('#cut-pos').fill('0.2');
  await page.locator('#cut').click();
  await expect(page.locator('#cut-pos')).toBeHidden();
  expect(errors).toEqual([]);
});

test.skip('X-Ray toggles on and off, and a model change turns it off', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html?model=rtx5070ti/card.json');
  await loaded(page);
  await page.locator('#xray').click();
  await expect(page.locator('#xray')).toHaveAttribute('aria-pressed', 'true');
  await page.locator('#xray').click();
  await expect(page.locator('#xray')).toHaveAttribute('aria-pressed', 'false');
  await page.locator('#xray').click();
  await page.getByRole('button', { name: /Pixel 6/ }).click();
  await loaded(page);
  await expect(page.locator('#xray')).toHaveAttribute('aria-pressed', 'false');
  expect(errors).toEqual([]);
});

test('Labels (L) tag the main parts and go away again', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/pcb.json');
  await loaded(page);
  await page.keyboard.press('l');
  await expect(page.locator('#labels')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.locator('.tag:not([hidden])').first()).toBeVisible();
  expect(await page.locator('.tag').count()).toBeGreaterThan(4);
  await page.keyboard.press('l');
  await expect(page.locator('.tag')).toHaveCount(0);
});

test('labels never overlap each other and stay under the landing card', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/tour.json');
  await expect(page.locator('#landing')).toBeVisible({ timeout: 90_000 });
  await page.keyboard.press('l');
  await expect(page.locator('#labels')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.locator('#tags')).toBeHidden();
  await page.locator('#explore-own').click();
  await expect(page.locator('#landing')).toBeHidden();
  await expect(page.locator('.tag:not([hidden])').first()).toBeVisible();
  const boxes = await page
    .locator('.tag:not([hidden])')
    .evaluateAll((els) =>
      els.map((e) => e.getBoundingClientRect()).map((r) => ({ x: r.x, y: r.y, w: r.width, h: r.height })),
    );
  for (let i = 0; i < boxes.length; i++)
    for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i];
      const b = boxes[j];
      const overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
      expect(overlap, `tags ${i} and ${j} overlap`).toBe(false);
    }
});

test('a project from a downloaded GLB shows its parts by name and credits its author', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/viewer.html?model=projects/motor/project.json');
  await loaded(page);
  await expect(page.locator('#credit')).toContainText('chiwei');
  await expect(page.locator('#credit')).toContainText('CC-BY-4.0');
  await expect(page.locator('#credit a')).toHaveAttribute('href', /sketchfab\.com/);
  await page.keyboard.press('l');
  const names = await page.locator('.tag:not([hidden])').allTextContents();
  expect(names).toContain('Bell');
  expect(names.some((n) => /Object_|polySurface|Cylinder_/.test(n))).toBe(false);
  expect(errors).toEqual([]);
});

test('level text rises into place on a level change, and stays still with reduced motion', async ({ page }) => {
  const moving = () =>
    page.evaluate(() =>
      ['.scale', '.ttl', '.txt'].map((s) => document.querySelector(`#hud ${s}`)!.getAnimations().length),
    );
  await page.goto('/viewer.html?model=rtx5070ti/tour.json&nointro');
  await loaded(page);
  await expect(page.locator('#hud .ttl')).toHaveText('GeForce RTX 5070 Ti');
  await page.keyboard.press('ArrowRight');
  await expect(page.locator('#hud .ttl')).toHaveText('Main PCB', { timeout: 60_000 });
  expect(await moving()).toEqual([1, 1, 1]);

  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.goto('/viewer.html?model=rtx5070ti/tour.json&nointro');
  await loaded(page);
  await page.keyboard.press('ArrowRight');
  await expect(page.locator('#hud .ttl')).toHaveText('Main PCB', { timeout: 60_000 });
  expect(await moving()).toEqual([0, 0, 0]);
});

test('on the landing, Esc explores and Enter starts the tour', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/tour.json');
  await expect(page.locator('#landing')).toBeVisible({ timeout: 90_000 });
  await page.keyboard.press('Escape');
  await expect(page.locator('#landing')).toBeHidden();
  await expect(page.locator('#auto')).toHaveAttribute('aria-pressed', 'false');

  await page.goto('/viewer.html?model=rtx5070ti/tour.json');
  await expect(page.locator('#landing')).toBeVisible({ timeout: 90_000 });
  await page.locator('body').focus();
  await page.keyboard.press('Enter');
  await expect(page.locator('#landing')).toBeHidden();
  await expect(page.locator('#auto')).toHaveAttribute('aria-pressed', 'true');
});

test('the ruler marker rides down as you zoom in', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/tour.json&nointro');
  await loaded(page);
  const top = () => page.locator('.ruler-marker').evaluate((e) => parseFloat((e as HTMLElement).style.top));
  await expect.poll(top).toBeGreaterThan(0);
  const before = await top();
  await page.mouse.move(480, 270);
  for (let i = 0; i < 6; i++) await page.mouse.wheel(0, -400);
  await expect.poll(top, { timeout: 10_000 }).toBeGreaterThan(before + 2);
});

test('a bad file shows an error instead of breaking the page', async ({ page }) => {
  await page.goto('/viewer.html?model=rtx5070ti/die.json');
  await loaded(page);
  await page.setInputFiles('#file', { name: 'notes.txt', mimeType: 'text/plain', buffer: Buffer.from('hi') });
  await expect(page.locator('#status')).toContainText("Can't open notes.txt");
});

test.describe('phone', () => {
  test.use({ viewport: { width: 390, height: 844 } });

  test('the model name opens a dropdown list', async ({ page }) => {
    await page.goto('/viewer.html?model=rtx5070ti/die.json');
    await loaded(page);
    await expect(page.locator('#library')).toBeHidden();
    await page.locator('#menu').click();
    await expect(page.locator('#library')).toBeVisible();
    await page.getByRole('button', { name: /Pixel 6/ }).click();
    await expect(page.locator('#library')).toBeHidden();
    await loaded(page);
    await expect(page.locator('#title')).toHaveText('Google Pixel 6');
  });
});
