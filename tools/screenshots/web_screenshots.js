// Renders web/index.html in headless Chromium against a mocked device API
// (status.fixture.json) and saves screenshots for the README.
// Usage: npm ci && npx playwright install chromium && npm run web [-- --out ../../img]
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '../..');
const outArg = process.argv.indexOf('--out');
const outDir = outArg > 0 ? path.resolve(process.argv[outArg + 1]) : path.join(root, 'img');
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
const status = JSON.parse(fs.readFileSync(path.join(__dirname, 'status.fixture.json'), 'utf8'));

const shots = [
    { name: 'web-mobile-de', locale: 'de-DE', viewport: { width: 390, height: 844 }, scale: 2 },
    { name: 'web-mobile-en', locale: 'en-US', viewport: { width: 390, height: 844 }, scale: 2 },
    { name: 'web-desktop-en', locale: 'en-US', viewport: { width: 1100, height: 900 }, scale: 1 },
];

(async () => {
    fs.mkdirSync(outDir, { recursive: true });
    const browser = await chromium.launch();
    let failed = false;
    for (const shot of shots) {
        const page = await browser.newPage({ locale: shot.locale, viewport: shot.viewport, deviceScaleFactor: shot.scale });
        page.on('pageerror', (e) => { failed = true; console.error(`${shot.name}: page error: ${e.message}`); });
        await page.route('**/*', (route) => {
            const url = new URL(route.request().url());
            if (url.pathname === '/') return route.fulfill({ contentType: 'text/html; charset=utf-8', body: html });
            if (url.pathname === '/api/status') return route.fulfill({ json: status });
            return route.fulfill({ status: 404, json: { error: 'not found' } });
        });
        await page.goto('http://gaszaehler.local/');
        await page.waitForFunction(() => document.getElementById('volume').textContent.trim() !== '-- m³');
        const file = path.join(outDir, `${shot.name}.png`);
        await page.screenshot({ path: file, fullPage: true });
        console.log(file);
        await page.close();
    }
    await browser.close();
    process.exit(failed ? 1 : 0);
})();
