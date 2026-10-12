const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  page.on('console', msg => console.log('LOG:', msg.text()));
  page.on('pageerror', err => console.log('PAGE ERROR:', err.message));
  await page.goto('http://localhost:8081', { waitUntil: 'networkidle0' });
  const res = await page.evaluate(() => {
    try {
      const rootEl = document.getElementById("root");
      const root = ReactDOM.createRoot(rootEl);
      root.render(React.createElement("h1", null, "TEST RENDER"));
      return "Render initiated";
    } catch(e) {
      return "ERROR: " + e.message;
    }
  });
  console.log("Result:", res);
  await new Promise(r => setTimeout(r, 1000));
  const html = await page.evaluate(() => document.body.innerHTML);
  console.log("HTML length:", html.length);
  await browser.close();
})();
