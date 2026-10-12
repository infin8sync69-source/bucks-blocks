const fs = require('fs');
const path = require('path');

const webDir = path.join(__dirname, 'web');
const frontendSrc = path.join(__dirname, 'frontend', 'src');

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

let content = "import React from 'react';\nimport ReactDOM from 'react-dom/client';\nimport './index.css';\n\n";
content += "window.React = React;\nwindow.ReactDOM = ReactDOM;\n\n";

for (const file of files) {
  content += `// --- ${file} ---\n`;
  let fileContent = fs.readFileSync(path.join(webDir, file), 'utf8');
  content += fileContent + '\n\n';
}

fs.writeFileSync(path.join(frontendSrc, 'main.jsx'), content);
console.log("Done");
