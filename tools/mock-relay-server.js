const http = require('http');

const server = http.createServer((req, res) => {
  if (req.method === 'POST' && req.url === '/api/relay') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const p = JSON.parse(body || '{}');
        const id = p.id || p.ID || '';
        const action = (p.action || '').toLowerCase();
        const state = action === 'on';
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, id, action, state }));
      } catch (e) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'invalid_json' }));
      }
    });
    return;
  }
  res.writeHead(404); res.end('Not found');
});

server.listen(3012, () => console.log('Mock relay HTTP server listening on http://localhost:3012'));
