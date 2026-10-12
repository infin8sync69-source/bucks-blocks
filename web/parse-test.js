const fs = require('fs');
const https = require('https');

https.get('https://unpkg.com/@babel/standalone@7.24.0/babel.min.js', (res) => {
  let data = '';
  res.on('data', chunk => data += chunk);
  res.on('end', () => {
    eval(data);
    const Babel = global.Babel;
    const files = [
      'tweaks-panel.jsx',
      'data.jsx',
      'components.jsx',
      'vault.jsx',
      'onboarding.jsx',
      'configure.jsx',
      'dashboard.jsx',
      'wallet.jsx',
      'app.jsx'
    ];
    let hasError = false;
    for (const file of files) {
      try {
        const content = fs.readFileSync(file, 'utf-8');
        Babel.transform(content, { presets: ['react'] });
        console.log(file + ' OK');
      } catch (e) {
        console.error('Error in ' + file + ':', e.message);
        hasError = true;
      }
    }
    if (!hasError) console.log('All files parsed successfully!');
  });
});
