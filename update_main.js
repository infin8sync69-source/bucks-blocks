const fs = require('fs');
const path = require('path');

const mainPath = path.join(__dirname, 'frontend', 'src', 'main.jsx');
let content = fs.readFileSync(mainPath, 'utf8');

// Principle 6: Semantic Action tokens
content = content.replace(/var\(--gold\)/g, 'var(--action)');
content = content.replace(/var\(--gold-deep\)/g, 'var(--action)');
content = content.replace(/var\(--gold-glow\)/g, 'var(--action)');
// Also in styles
content = content.replace(/gold:/g, 'action:');

// Principle 7: Session Idle Lock
// Add to DataProvider a simple idle timer that locks the UI
const idleLockCode = `
  const [isLocked, setIsLocked] = React.useState(false);
  
  React.useEffect(() => {
    let timeout;
    const reset = () => {
      clearTimeout(timeout);
      timeout = setTimeout(() => setIsLocked(true), 5 * 60 * 1000); // 5 mins
    };
    window.addEventListener('mousemove', reset);
    window.addEventListener('keydown', reset);
    reset();
    return () => {
      window.removeEventListener('mousemove', reset);
      window.removeEventListener('keydown', reset);
      clearTimeout(timeout);
    };
  }, []);

  if (isLocked) {
    return (
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', background: 'var(--surface)', color: 'var(--text)' }}>
        <Icons.lock size={48} style={{ color: 'var(--warning)', marginBottom: 20 }} />
        <h2>Session Locked</h2>
        <p style={{ color: 'var(--text-secondary)', marginBottom: 20 }}>Idle for 5 minutes.</p>
        <Button variant="action" onClick={() => setIsLocked(false)}>Unlock</Button>
      </div>
    );
  }
`;
content = content.replace(/function DataProvider\(\{ children \}\) \{/, 'function DataProvider({ children }) {\n' + idleLockCode);

// Principle 3: Avatar based confirm
// Update peer rendering to use avatar
content = content.replace(
  /<div className="mono" style=\{\{ fontSize: 13, color: "var\(--ink-2\)" \}\}>\{p\.address\}<\/div>/g,
  '<div style={{ display: "flex", alignItems: "center", gap: 8 }}><div className="avatar" style={{ width: 24, height: 24, fontSize: 10, background: "var(--violet)" }}>{p.address.substring(0, 2)}</div><div className="mono" style={{ fontSize: 13, color: "var(--ink-2)" }}>{p.address}</div></div>'
);

fs.writeFileSync(mainPath, content);
console.log("Updated main.jsx");
