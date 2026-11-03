import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const dist = path.join(root, 'dist');
const htmlPath = path.join(dist, 'index.html');
let html = fs.readFileSync(htmlPath, 'utf8');

html = html.replace(/\s*<script type="importmap">[\s\S]*?<\/script>\s*/i, '\n');

const script = html.match(
  /<script type="module"[^>]*src="([^"]+)"[^>]*><\/script>/i);
if (!script) {
  throw new Error('no built module script found in dist/index.html');
}
const jsPath = path.join(dist, script[1].replace(/^\//, ''));
const js = fs.readFileSync(jsPath, 'utf8');
if (js.includes('</script')) {
  throw new Error('built JavaScript contains </script');
}
html = html.replace(script[0], `<script type="module">\n${js}\n</script>`);

const out = path.join(root, 'viewer-standalone.html');
fs.writeFileSync(out, html);
console.log(`wrote ${out} (${(html.length / 1024).toFixed(0)} KiB)`);
