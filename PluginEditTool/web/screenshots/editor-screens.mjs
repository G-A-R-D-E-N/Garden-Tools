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
  const scoped = `${key}@${args?.[1]}`;
  const source = scoped in fixtures ? scoped : key;
  const found = source in fixtures ? fixtures[source] : fixtures.default;
  const value = key in transforms ? transforms[key](found, args ?? []) : found;
  return { ok: true, value: typeof value === 'string' ? value : JSON.stringify(value) };
}

async function chooseRecord(page, record) {
  await page.getByRole('button', { name: 'Choose', exact: true }).click();
  await page.getByText(record, { exact: true }).click();
  await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
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
  {
    name: 'quest-check',
    label: 'Quest',
    async pick(page) {
      await page.getByRole('button', { name: 'Choose quest' }).click();
      await page.getByText('MissingCourier', { exact: true }).click();
      await page.getByRole('button', { name: 'Check this quest', exact: true }).click();
      await page.locator('.quest-check-counts').waitFor();
    },
  },
  {
    name: 'wizard-templates',
    label: 'New quest',
    async pick(page) {
      for (const title of ['Fetch an item', 'Kill a target', 'Talk to someone', 'Escort', 'Choice with two endings']) {
        await page.getByRole('button', { name: new RegExp(`^${title}`) }).waitFor();
      }
    },
  },
  {
    name: 'wizard-fetch-review',
    label: 'New quest',
    async pick(page) {
      await page.getByRole('button', { name: /^Fetch an item/ }).click();
      await page.getByLabel('Which plugin should the quest go in?').selectOption('Patch.esp');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.getByLabel("What is the quest's internal name?").fill('LostLocket');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await chooseRecord(page, 'CourierMother');
      await chooseRecord(page, 'OldRailYard');
      await chooseRecord(page, 'CourierLocket');
      await page.getByLabel('What does the player get as a reward?').fill('50 caps');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.locator('.wizard-review').waitFor();
    },
  },
  {
    name: 'wizard-choice-review',
    label: 'New quest',
    async pick(page) {
      await page.getByRole('button', { name: /^Choice with two endings/ }).click();
      await page.getByLabel('Which plugin should the quest go in?').selectOption('Patch.esp');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.getByLabel("What is the quest's internal name?").fill('RaiderDeal');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await chooseRecord(page, 'CourierMother');
      await page.getByLabel('What is the first choice?').fill('Spare the raiders');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.getByLabel('What is the second choice?').fill('Hand the raiders over');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.getByLabel('What does the player get for the first choice?').fill('50 caps');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.getByLabel('What does the player get for the second choice?').fill('20 caps');
      await page.locator('.wizard-nav').getByRole('button', { name: 'Next', exact: true }).click();
      await page.locator('.wizard-review').waitFor();
    },
  },
  {
    name: 'quest-flow-chart',
    label: 'Quest',
    async pick(page) {
      await page.getByRole('button', { name: 'Choose quest' }).click();
      await page.getByText('FlowChartDemo', { exact: true }).click();
      await page.locator('.quest-flow-arrow-jump').waitFor();
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
        await page.locator('.editors-modal').getByRole('button', { name: 'Help', exact: true }).click();
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
