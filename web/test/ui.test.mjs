// Run: node web/test/ui.test.mjs
// DOM-free parts of ui.js; the element is a stub with only replaceChildren.
import assert from "node:assert/strict";
import { keyedRender } from "../js/ui.js";

// keyedRender skips an unchanged signature, and rebuilds after reset() even when it is the same (ONB-06:
// Cancel on the create form wrote into the holder directly and the main view never came back).
{
  const el = { builds: 0, replaceChildren() { this.builds++; } };
  const draw = keyedRender(el);
  draw(["main", 1], () => "a");
  draw(["main", 1], () => "a");
  assert.equal(el.builds, 1);
  draw(["main", 2], () => "b");
  assert.equal(el.builds, 2);
  draw.reset();
  draw(["main", 2], () => "b");
  assert.equal(el.builds, 3);
}

console.log("ui.test.mjs: ok");
