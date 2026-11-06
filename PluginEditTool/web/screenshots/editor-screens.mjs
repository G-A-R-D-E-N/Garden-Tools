import { chromium } from 'playwright';
import { mkdirSync, readFileSync } from 'node:fs';
import { spawn } from 'node:child_process';

const OUT = process.env.SHOT_DIR ?? 'screenshots/out';
const PORT = 4173;
const fixtures = JSON.parse(readFileSync(new URL('./fixtures.json', import.meta.url), 'utf8'));

function reply(target, method) {
  const key = `${target}.${method}`;
  const value = key in fixtures ? fixtures[key] : fixtures.default;
  return { ok: true, value: typeof value === 'string' ? value : JSON.stringify(value) };
}

async function main() {
  mkdirSync(OUT, { recursive: true });
  const server = spawn('npx', ['vite', 'preview', '--port', String(PORT), '--strictPort'], { stdio: 'inherit', shell: true });
  const browser = await chromium.launch();
  try {
    for (const theme of ['dark', 'light']) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, colorScheme: theme });
      await page.route('**/rpc', async route => {
        const { target, method } = route.request().postDataJSON();
        await route.fulfill({ json: reply(target, method) });
      });
      await page.route('**/events', route => route.fulfill({ status: 200, contentType: 'text/event-stream', body: '' }));
      await waitForServer();
      await page.goto(`http://localhost:${PORT}/`);
      await page.getByTitle(/^Editors/).click();
      await page.screenshot({ path: `${OUT}/${theme}-0-editors-home.png` });
      for (const [i, name] of ['Quest', 'Dialogue', 'Scene'].entries()) {
        await page.getByRole('button', { name: new RegExp(`^${name}`) }).first().click();
        await page.waitForTimeout(400);
        await page.screenshot({ path: `${OUT}/${theme}-${i + 1}-${name.toLowerCase()}.png` });
        const help = page.getByRole('button', { name: 'Help', exact: true });
        if (await help.count()) {
          await help.first().click();
          await page.waitForTimeout(200);
          await page.screenshot({ path: `${OUT}/${theme}-${i + 1}-${name.toLowerCase()}-help.png` });
        }
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
