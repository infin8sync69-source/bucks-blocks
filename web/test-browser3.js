const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  const res = await page.evaluate(() => {
    try {
      Object.defineProperty(window, "X", { value: 1, writable: false, configurable: true });
      Object.defineProperty(window, "X", { value: 1, writable: false, configurable: true });
      return "Success";
    } catch(e) {
      return e.message;
    }
  });
  console.log("Result:", res);
  await browser.close();
})();
