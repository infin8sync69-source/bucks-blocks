const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  
  page.on('console', msg => console.log('LOG:', msg.text()));
  page.on('pageerror', err => console.log('PAGE ERROR:', err.message));
  
  await page.goto('http://localhost:8081', { waitUntil: 'networkidle0' });
  
  const html = await page.evaluate(() => document.body.innerHTML);
  console.log("BODY HTML:", html);
  
  const hasApp = await page.evaluate(() => typeof App !== 'undefined');
  console.log("App defined:", hasApp);
  
  await browser.close();
})();
