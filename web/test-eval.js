const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  page.on('console', msg => console.log('LOG:', msg.text()));
  await page.goto('http://localhost:8081', { waitUntil: 'networkidle0' });
  await browser.close();
})();
