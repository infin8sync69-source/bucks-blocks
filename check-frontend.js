const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  page.on('console', msg => console.log('LOG:', msg.text()));
  page.on('pageerror', err => console.log('PAGE ERROR:', err.message));
  await page.goto('http://localhost:8081', { waitUntil: 'networkidle0' });
  await new Promise(r => setTimeout(r, 2000));
  await page.screenshot({ path: 'frontend-success.png' });
  console.log("Screenshot saved to frontend-success.png");
  await browser.close();
})();
