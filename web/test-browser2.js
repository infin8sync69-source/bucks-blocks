const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  const res = await page.evaluate(() => {
    try {
      eval('const X = 1; Object.defineProperty(window, "X", { value: X, writable: false });');
      return "Success";
    } catch(e) {
      return e.message;
    }
  });
  console.log("Result:", res);
  await browser.close();
})();
