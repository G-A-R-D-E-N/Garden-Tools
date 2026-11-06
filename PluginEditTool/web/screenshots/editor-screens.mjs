import { chromium } from 'playwright';
import { mkdirSync, readFileSync } from 'node:fs';
import { spawn } from 'node:child_process';

const OUT = process.env.SHOT_DIR ?? 'screenshots/out';
const PORT = 4173;
const fixtures = JSON.parse(readFileSync(new URL('./fixtures.json', import.meta.url), 'utf8'));

const transforms = {
  'backend.SearchRecords': (hits, [, typeFilter]) => hits.filter(hit => !typeFilter || hit.Type === typeFilter),
};

function reply(target, method, args) {
  const key = `${target}.${method}`;
  const found = key in fixtures ? fixtures[key] : fixtures.default;
  const value = key in transforms ? transforms[key](found, args ?? []) : found;
  return { ok: true, value: typeof value === 'string' ? value : JSON.stringify(value) };
}

const FLOWS = [
  {
    name: 'quest',
    label: 'Quest',
    async pick(page) {
      await page.getByRole('button', { name: 'Choose quest' }).click();
      await page.getByText('MissingCourier', { exact: true }).click();
      await page.getByLabel('Stage 10 log entry 1 text').waitFor();
    },
  },
  {
    name: 'dialogue',
    label: 'Dialogue',
    async pick(page) {
      await page.getByRole('button', { name: 'Pick a quest' }).click();
      await page.getByText('MissingCourier', { exact: true }).click();
      await page.getByRole('button', { name: /Have you seen my son/ }).first().click();
      await page.getByRole('button', { name: /never came back/ }).waitFor();
    },
  },
  {
    name: 'scene',
    label: 'Scene',
    async pick(page) {
      await page.getByRole('button', { name: 'Pick a scene' }).click();
      await page.getByText('BagInTheAlley', { exact: true }).click();
      await page.locator('.scene-title', { hasText: 'BagInTheAlley' }).waitFor();
    },
  },
];

async function main() {
  mkdirSync(OUT, { recursive: true });
  const server = spawn('npx', ['vite', 'preview', '--port', String(PORT), '--strictPort'], { stdio: 'inherit', shell: true });
  const browser = await chromium.launch();
  try {
    for (const theme of ['dark', 'light']) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, colorScheme: theme });
      await page.route('**/rpc', async route => {
        const { target, method, args } = route.request().postDataJSON();
        await route.fulfill({ json: reply(target, method, args) });
      });
      await page.route('**/events', route => route.fulfill({ status: 200, contentType: 'text/event-stream', body: '' }));
      await waitForServer();
      await page.goto(`http://localhost:${PORT}/`);
      await page.getByTitle(/^Editors/).click();
      await page.screenshot({ path: `${OUT}/${theme}-0-editors-home.png` });
      for (const [i, flow] of FLOWS.entries()) {
        await page.getByRole('button', { name: new RegExp(`^${flow.label}`) }).first().click();
        await flow.pick(page);
        await page.screenshot({ path: `${OUT}/${theme}-${i + 1}-${flow.name}.png` });
        await page.getByRole('button', { name: 'Help', exact: true }).click();
        await page.locator('.help-drawer').waitFor();
        await page.screenshot({ path: `${OUT}/${theme}-${i + 1}-${flow.name}-help.png` });
        await page.getByRole('button', { name: 'Close help' }).click();
        await page.getByRole('button', { name: /Back/ }).first().click();
      }
      await page.close();
    }
  } finally {
    await browser.close();
    server.kill();
  }
}

async function waitForServer() {
  for (let i = 0; i < 60; i++) {
    try { if ((await fetch(`http://localhost:${PORT}/`)).ok) return; } catch { /* retry */ }
    await new Promise(r => setTimeout(r, 500));
  }
  throw new Error('preview server did not start');
}

main().catch(err => { console.error(err); process.exit(1); });
