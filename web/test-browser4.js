const puppeteer = require('puppeteer');
(async () => {
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage();
  const res = await page.evaluate(() => {
    var TEST_VAR = 1;
    const desc = Object.getOwnPropertyDescriptor(window, 'TEST_VAR');
    return desc ? desc.configurable : "Not found";
  });
  console.log("Configurable:", res);
  await browser.close();
})();
