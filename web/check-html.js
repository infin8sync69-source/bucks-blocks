const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  await page.goto('http://localhost:8081', { waitUntil: 'networkidle0' });
  await new Promise(r => setTimeout(r, 2000));
  const html = await page.evaluate(() => document.getElementById('root').innerHTML);
  console.log("ROOT HTML:\n", html);
  await browser.close();
})();
