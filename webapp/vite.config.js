import { defineConfig } from 'vite';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const patternsDir = fileURLToPath(new URL('../patterns', import.meta.url));

function contentHash(text) {
  return crypto.createHash('sha1').update(text).digest('hex');
}

function sendJson(res, status, value) {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.setHeader('Cache-Control', 'no-store');
  res.end(JSON.stringify(value));
}

function safePatternName(name) {
  return typeof name === 'string' &&
    name.endsWith('.anim.json') &&
    path.basename(name) === name;
}

function patternApi() {
  return {
    name: 'suspended-spheres-patterns',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = new URL(req.url || '/', 'http://localhost');

        if (url.pathname === '/api/patterns' && req.method === 'GET') {
          let files = [];
          try {
            files = fs.readdirSync(patternsDir)
              .filter((f) => f.endsWith('.anim.json'))
              .sort();
          } catch {
            /* no patterns directory: return an empty catalogue */
          }
          const patterns = files.map((name) => ({
            name,
            hash: contentHash(fs.readFileSync(path.join(patternsDir, name), 'utf8')),
          }));
          sendJson(res, 200, { patterns });
          return;
        }

        if (url.pathname === '/api/pattern' && req.method === 'GET') {
          const name = url.searchParams.get('name') || '';
          if (!safePatternName(name)) {
            sendJson(res, 400, { error: 'invalid pattern name' });
            return;
          }
          try {
            const text = fs.readFileSync(path.join(patternsDir, name), 'utf8');
            sendJson(res, 200, { name, text, hash: contentHash(text) });
          } catch {
            sendJson(res, 404, { error: 'pattern not found' });
          }
          return;
        }

        if (url.pathname === '/api/report' && req.method === 'POST') {
          let body = '';
          req.on('data', (chunk) => { body += chunk; });
          req.on('end', () => {
            try {
              const data = JSON.parse(body);
              if (!safePatternName(data.name)) {
                sendJson(res, 400, { error: 'invalid pattern name' });
                return;
              }
              const reportName = data.name.replace(/\.anim\.json$/, '.report.json');
              const text = typeof data.report === 'string'
                ? data.report
                : JSON.stringify(data.report);
              fs.writeFileSync(path.join(patternsDir, reportName),
                               text.endsWith('\n') ? text : text + '\n');
              sendJson(res, 200, { ok: true, report: reportName });
            } catch {
              sendJson(res, 400, { error: 'invalid report body' });
            }
          });
          return;
        }

        next();
      });
    },
  };
}

export default defineConfig({
  root: '.',
  publicDir: 'public',
  plugins: [patternApi()],
  build: {
    outDir: 'dist',
  },
});
