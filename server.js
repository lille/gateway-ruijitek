const http = require('http');
const fs = require('fs');
const path = require('path');
const url = require('url');

const rootDir = __dirname;
const mockPath = path.join(rootDir, 'data', 'mock-data.json');
const mockData = JSON.parse(fs.readFileSync(mockPath, 'utf8'));
const gatewayLatestPath = path.join(rootDir, 'data', 'gateway-latest.json');
const remoteDashboardUrl = process.env.REMOTE_DASHBOARD_URL || 'http://192.168.10.111:8090/api/dashboard';

const mimeTypes = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp'
};

function sendJson(response, statusCode, payload) {
  response.writeHead(statusCode, { 'Content-Type': 'application/json; charset=utf-8' });
  response.end(JSON.stringify(payload, null, 2));
}

function sendText(response, statusCode, text, contentType = 'text/plain; charset=utf-8') {
  response.writeHead(statusCode, { 'Content-Type': contentType });
  response.end(text);
}

function getStaticPath(requestPath) {
  const safePath = requestPath === '/' ? '/web-demo/index.html' : requestPath;
  const normalized = path.normalize(decodeURIComponent(safePath)).replace(/^\.(?:\\|\/)+/, '');
  const candidate = path.join(rootDir, normalized);
  if (!candidate.startsWith(rootDir)) {
    return null;
  }
  return candidate;
}

function getGatewayDevices() {
  return mockData.gateways;
}

function getDevices() {
  return mockData.devices;
}

function createHistory(pointId) {
  if (pointId === 'pressure') {
    return mockData.history.pressure;
  }
  return mockData.history.temp;
}

function readGatewayLatest() {
  try {
    if (fs.existsSync(gatewayLatestPath)) {
      const txt = fs.readFileSync(gatewayLatestPath, 'utf8');
      if (txt && txt.trim()) {
        return JSON.parse(txt);
      }
    }
  } catch (e) {
    console.warn('Failed reading gateway-latest.json', e && e.message);
  }
  return null;
}

function proxyGetJson(targetUrl, cb) {
  try {
    const parsed = url.parse(targetUrl);
    const client = parsed.protocol === 'https:' ? require('https') : require('http');
    const opts = {
      hostname: parsed.hostname,
      port: parsed.port,
      path: parsed.path,
      method: 'GET',
      timeout: 5000
    };
    const req = client.request(opts, (res) => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (chunk) => body += chunk);
      res.on('end', () => {
        try {
          const json = JSON.parse(body || '{}');
          cb(null, json);
        } catch (e) {
          cb(new Error('Invalid JSON from remote'));
        }
      });
    });
    req.on('error', (err) => cb(err));
    req.on('timeout', () => { req.destroy(new Error('Timeout')); });
    req.end();
  } catch (e) {
    cb(e);
  }
}

const server = http.createServer((request, response) => {
  const parsedUrl = url.parse(request.url, true);
  const pathname = parsedUrl.pathname || '/';

  if (pathname === '/api/summary') {
    return sendJson(response, 200, mockData.summary);
  }

  if (pathname === '/api/gateways') {
    return sendJson(response, 200, getGatewayDevices());
  }

  if (pathname === '/api/devices') {
    return sendJson(response, 200, getDevices());
  }

  if (pathname === '/api/realtime') {
    const deviceId = parsedUrl.query.deviceId || 'DEV-2001';
    const latest = readGatewayLatest();
    if (latest && latest[deviceId]) {
      return sendJson(response, 200, latest[deviceId]);
    }
    return sendJson(response, 200, mockData.realtime[deviceId] || []);
  }

  if (pathname === '/api/history') {
    const pointId = parsedUrl.query.pointId || 'temp';
    return sendJson(response, 200, createHistory(pointId));
  }

  if (pathname === '/api/alarms') {
    return sendJson(response, 200, mockData.alarms);
  }

  if (pathname === '/api/dashboard') {
    // 默认从远端 Dashboard 代理数据，如果不可用则回退到本地 mock
    return proxyGetJson(remoteDashboardUrl, (err, json) => {
      if (err) {
        return sendJson(response, 200, {
          summary: mockData.summary,
          gateways: mockData.gateways,
          devices: mockData.devices,
          alarms: mockData.alarms.slice(0, 3),
          _proxyError: err.message
        });
      }
      return sendJson(response, 200, json);
    });
  }

  if (pathname === '/api/remote-dashboard') {
    return proxyGetJson(remoteDashboardUrl, (err, json) => {
      if (err) return sendJson(response, 502, { error: err.message });
      return sendJson(response, 200, json);
    });
  }

  if (pathname === '/api/debug-gateway-latest') {
    const latest = readGatewayLatest();
    return sendJson(response, 200, { latest });
  }

  const staticPath = getStaticPath(pathname);
  if (!staticPath) {
    return sendText(response, 403, 'Forbidden');
  }

  fs.stat(staticPath, (statError, stats) => {
    if (statError || !stats.isFile()) {
      return sendText(response, 404, 'Not Found');
    }

    const ext = path.extname(staticPath).toLowerCase();
    const contentType = mimeTypes[ext] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': contentType });
    fs.createReadStream(staticPath).pipe(response);
  });
});

const port = Number(process.env.PORT || 3010);

server.listen(port, () => {
  console.log(`Mock demo server running at http://localhost:${port}`);
  console.log('Available endpoints: /api/summary, /api/gateways, /api/devices, /api/realtime, /api/history, /api/alarms, /api/dashboard');
});