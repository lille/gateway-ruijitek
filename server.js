const http = require('http');
const fs = require('fs');
const path = require('path');
const url = require('url');

const rootDir = __dirname;
const mockPath = path.join(rootDir, 'data', 'mock-data.json');
const mockData = JSON.parse(fs.readFileSync(mockPath, 'utf8'));
const gatewayLatestPath = path.join(rootDir, 'data', 'gateway-latest.json');
const remoteDashboardUrl = process.env.REMOTE_DASHBOARD_URL || 'http://192.168.10.111:8090/api/dashboard';
const remoteRelayUrl = process.env.REMOTE_RELAY_URL || '';
// 当不希望/不能通过 HTTP 访问网关时，可通过 SSH 执行远端写入命令，格式如 user@192.168.10.200
const remoteRelaySsh = process.env.REMOTE_RELAY_SSH || '';
// 可选的 SSH 私钥路径（用于通过 ssh -i <key> 登录远端执行命令）
const remoteRelaySshKey = process.env.REMOTE_RELAY_SSH_KEY || '';
// 可选的代理访问 token（用于 /api/relay/proxy）
const relayProxyToken = process.env.RELAY_PROXY_TOKEN || '';

// runtime-config 可在运行时通过 /api/relay/config 修改（优先于环境变量）
let runtimeRemoteRelayUrl = remoteRelayUrl;
let runtimeRemoteRelaySsh = remoteRelaySsh;
let runtimeRelayProxyToken = relayProxyToken;
let runtimeRemoteRelaySshKey = remoteRelaySshKey;

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

// 内存中的继电器状态（示例：{ "LED-1": true }）
const relayState = {};

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

function appendRelayLog(entry) {
  try {
    const logDir = path.join(rootDir, 'logs');
    if (!fs.existsSync(logDir)) fs.mkdirSync(logDir, { recursive: true });
    const p = path.join(logDir, 'relay-ops.log');
    const line = JSON.stringify(entry) + '\n';
    fs.appendFile(p, line, (err) => { if (err) console.warn('Failed to append relay log', err); });
  } catch (e) {
    console.warn('appendRelayLog error', e && e.message);
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

  // 继电器控制：接收 POST JSON { id: string, action: 'on'|'off' }
  if (pathname === '/api/relay' && request.method === 'POST') {
    let body = '';
    request.on('data', (chunk) => body += chunk);
    request.on('end', () => {
      try {
        const payload = JSON.parse(body || '{}');
        const id = payload.id;
        const action = payload.action;
        if (!id || (action !== 'on' && action !== 'off')) {
          return sendJson(response, 400, { error: 'invalid_payload' });
        }
        relayState[id] = action === 'on';
        return sendJson(response, 200, { id, action, state: relayState[id] });
      } catch (e) {
        return sendJson(response, 400, { error: 'invalid_json' });
      }
    });
    return;
  }

  // 代理继电器控制到远端网关（需要设置环境变量 REMOTE_RELAY_URL）
  if (pathname === '/api/relay/proxy' && request.method === 'POST') {
    // 简单 token 校验：如果设置了 runtimeRelayProxyToken，则要求客户端在
    // Authorization: Bearer <token> 或 X-API-KEY 头中提供相同 token
    if (runtimeRelayProxyToken) {
      const authHeader = (request.headers && (request.headers['authorization'] || request.headers['x-api-key'])) || '';
      let token = '';
      if (typeof authHeader === 'string') {
        token = authHeader;
      } else if (Array.isArray(authHeader) && authHeader.length) {
        token = authHeader[0];
      }
      if (token.toLowerCase().startsWith('bearer ')) {
        token = token.slice(7).trim();
      }
      if (token !== runtimeRelayProxyToken) {
        return sendJson(response, 401, { error: 'unauthorized' });
      }
    }

    let body = '';
    request.on('data', (chunk) => body += chunk);
    request.on('end', () => {
      // 解析请求体以便记录日志（若不是 JSON 则空对象）
      let payload = {};
      try { payload = JSON.parse(body || '{}'); } catch (e) { /* ignore */ }
      const id = payload.id || payload.ID || '';
      const action = (payload.action || '').toLowerCase();

      // If REMOTE_RELAY_URL is configured (runtime), forward via HTTP (existing behavior)
      if (runtimeRemoteRelayUrl) {
        try {
          const parsed = url.parse(runtimeRemoteRelayUrl);
          const client = parsed.protocol === 'https:' ? require('https') : require('http');
          const opts = {
            hostname: parsed.hostname,
            port: parsed.port,
            path: parsed.path || '/api/relay',
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'Content-Length': Buffer.byteLength(body)
            },
            timeout: 5000
          };
          const req2 = client.request(opts, (res2) => {
            let resp = '';
            res2.setEncoding('utf8');
            res2.on('data', (c) => resp += c);
            res2.on('end', () => {
              try {
                const json = JSON.parse(resp || '{}');
                appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'http', target: runtimeRemoteRelayUrl, id, action, result: json });
                return sendJson(response, 200, json);
              } catch (e) {
                appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'http', target: runtimeRemoteRelayUrl, id, action, error: 'invalid_json_from_remote' });
                return sendJson(response, 502, { error: 'invalid_json_from_remote' });
              }
            });
          });
          req2.on('error', (err) => { appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'http', target: runtimeRemoteRelayUrl, id, action, error: err.message }); return sendJson(response, 502, { error: err.message }); });
          req2.on('timeout', () => { req2.destroy(new Error('Timeout')); });
          req2.write(body);
          req2.end();
        } catch (e) {
          appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'http', target: runtimeRemoteRelayUrl, id, action, error: e.message });
          return sendJson(response, 502, { error: e.message });
        }
        return;
      }

      // 如果配置了 REMOTE_RELAY_SSH（runtime），则通过 SSH 在远端执行写入命令（无需修改板子程序）
      if (runtimeRemoteRelaySsh) {
        try {
          if (!id || (action !== 'on' && action !== 'off')) {
            appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'ssh', target: runtimeRemoteRelaySsh, id, action, error: 'invalid_payload' });
            return sendJson(response, 400, { error: 'invalid_payload' });
          }
          const value = action === 'on' ? '1' : '0';
          const cmd = `echo "${id} ${value}" > /proc/proembed/gpio`;
          const { exec } = require('child_process');
          // 使用 ssh 执行命令；支持可选的私钥路径
          let sshCmd = `ssh -o BatchMode=yes`;
          if (runtimeRemoteRelaySshKey) {
            sshCmd += ` -i '${runtimeRemoteRelaySshKey.replace(/'/g,"'\\''")}' -o IdentitiesOnly=yes`;
          }
          sshCmd += ` ${runtimeRemoteRelaySsh} '${cmd.replace(/'/g,"'\\''")}'`;
          exec(sshCmd, { timeout: 5000 }, (err, stdout, stderr) => {
            if (err) {
              appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'ssh', target: runtimeRemoteRelaySsh, id, action, ok: false, error: (stderr || err.message) });
              return sendJson(response, 502, { error: 'ssh_failed', detail: (stderr || err.message) });
            }
            appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'ssh', target: runtimeRemoteRelaySsh, id, action, ok: true });
            return sendJson(response, 200, { ok: true, id, action });
          });
        } catch (e) {
          appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'ssh', target: runtimeRemoteRelaySsh, id, action, error: e.message });
          return sendJson(response, 502, { error: e.message });
        }
        return;
      }

      return sendJson(response, 502, { error: 'no_remote_configured' });
    });
    return;
  }

  if (pathname === '/api/remote-dashboard') {
    return proxyGetJson(remoteDashboardUrl, (err, json) => {
      if (err) return sendJson(response, 502, { error: err.message });
      return sendJson(response, 200, json);
    });
  }

  // 业务触发示例：触发继电器（用于 demo 和集成测试）
  if (pathname === '/api/business/trigger') {
    if (request.method !== 'POST') return sendJson(response, 405, { error: 'method' });
    let body = '';
    request.on('data', (c) => body += c);
    request.on('end', () => {
      try {
        const payload = JSON.parse(body || '{}');
        const id = payload.id;
        const action = (payload.action || '').toLowerCase();
        const mode = payload.mode || 'proxy'; // demo | direct | proxy
        const target = payload.target || '';
        const token = payload.token || '';
        const sshKey = payload.sshKey || '';
        if (!id || (action !== 'on' && action !== 'off')) return sendJson(response, 400, { error: 'invalid_payload' });

        // demo 模式：直接在内存修改状态
        if (mode === 'demo') {
          relayState[id] = (action === 'on');
          appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'demo', target: 'local', id, action, ok: true });
          return sendJson(response, 200, { ok: true, id, action, state: relayState[id] });
        }

        // direct 模式：直接请求远端网关（target 必须为完整 URL）
        if (mode === 'direct') {
          const urlTarget = target || runtimeRemoteRelayUrl;
          if (!urlTarget) return sendJson(response, 400, { error: 'no_target' });
          try {
            const parsed = url.parse(urlTarget);
            const client = parsed.protocol === 'https:' ? require('https') : require('http');
            const body2 = JSON.stringify({ id, action });
            const opts = {
              hostname: parsed.hostname,
              port: parsed.port,
              path: parsed.path || '/api/relay',
              method: 'POST',
              headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(body2)
              },
              timeout: 5000
            };
            const req2 = client.request(opts, (res2) => {
              let resp = '';
              res2.setEncoding('utf8');
              res2.on('data', (c) => resp += c);
              res2.on('end', () => {
                try {
                  const json = JSON.parse(resp || '{}');
                  appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'direct', target: urlTarget, id, action, result: json });
                  return sendJson(response, 200, json);
                } catch (e) {
                  appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'direct', target: urlTarget, id, action, error: 'invalid_json' });
                  return sendJson(response, 502, { error: 'invalid_json_from_target' });
                }
              });
            });
            req2.on('error', (err) => { appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'direct', target: urlTarget, id, action, error: err.message }); return sendJson(response, 502, { error: err.message }); });
            req2.write(body2); req2.end();
          } catch (e) {
            appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'direct', target: urlTarget, id, action, error: e.message });
            return sendJson(response, 502, { error: e.message });
          }
          return;
        }

        // proxy 模式：调用本服务的 /api/relay/proxy（使用 token/sshKey 可覆盖运行时配置）
        if (mode === 'proxy') {
          // 如果传入临时 override，则写入运行时后再调用
          const prevUrl = runtimeRemoteRelayUrl;
          const prevSsh = runtimeRemoteRelaySsh;
          const prevSshKey = runtimeRemoteRelaySshKey;
          const prevToken = runtimeRelayProxyToken;
          if (target && target.startsWith('http')) runtimeRemoteRelayUrl = target;
          else if (target) runtimeRemoteRelaySsh = target;
          if (sshKey) runtimeRemoteRelaySshKey = sshKey;
          if (token) runtimeRelayProxyToken = token;

          // 内部调用 /api/relay/proxy
          const postBody = JSON.stringify({ id, action });
          const client = require('http');
          const opts = {
            hostname: 'localhost', port: Number(process.env.PORT || 3011), path: '/api/relay/proxy', method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(postBody) }, timeout: 5000
          };
          if (runtimeRelayProxyToken) opts.headers['Authorization'] = 'Bearer ' + runtimeRelayProxyToken;
          const req2 = client.request(opts, (res2) => {
            let resp = '';
            res2.setEncoding('utf8'); res2.on('data', (c) => resp += c);
            res2.on('end', () => {
              try {
                const json = JSON.parse(resp || '{}');
                appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'proxy', target: runtimeRemoteRelayUrl || runtimeRemoteRelaySsh, id, action, result: json });
                // 恢复运行时覆盖
                runtimeRemoteRelayUrl = prevUrl; runtimeRemoteRelaySsh = prevSsh; runtimeRemoteRelaySshKey = prevSshKey; runtimeRelayProxyToken = prevToken;
                return sendJson(response, 200, json);
              } catch (e) {
                appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'proxy', target: runtimeRemoteRelayUrl || runtimeRemoteRelaySsh, id, action, error: 'invalid_json' });
                runtimeRemoteRelayUrl = prevUrl; runtimeRemoteRelaySsh = prevSsh; runtimeRemoteRelaySshKey = prevSshKey; runtimeRelayProxyToken = prevToken;
                return sendJson(response, 502, { error: 'invalid_json_from_proxy' });
              }
            });
          });
          req2.on('error', (err) => { appendRelayLog({ timestamp: new Date().toISOString(), clientIp: request.socket && request.socket.remoteAddress, mode: 'proxy', target: runtimeRemoteRelayUrl || runtimeRemoteRelaySsh, id, action, error: err.message }); runtimeRemoteRelayUrl = prevUrl; runtimeRemoteRelaySsh = prevSsh; runtimeRemoteRelaySshKey = prevSshKey; runtimeRelayProxyToken = prevToken; return sendJson(response, 502, { error: err.message }); });
          req2.write(postBody); req2.end();
          return;
        }

        return sendJson(response, 400, { error: 'unknown_mode' });
      } catch (e) {
        return sendJson(response, 400, { error: 'invalid_json' });
      }
    });
    return;
  }

  // 运行时设置 relay 代理配置（用于 demo，无需重启服务）
  if (pathname === '/api/relay/config') {
    if (request.method === 'GET') {
      return sendJson(response, 200, {
        remoteRelayUrl: runtimeRemoteRelayUrl,
        remoteRelaySsh: runtimeRemoteRelaySsh,
        remoteRelaySshKey: runtimeRemoteRelaySshKey,
        relayProxyToken: runtimeRelayProxyToken
      });
    }
    if (request.method === 'POST') {
      let body = '';
      request.on('data', (c) => body += c);
      request.on('end', () => {
        try {
          const payload = JSON.parse(body || '{}');
          if (typeof payload.remoteRelayUrl === 'string') runtimeRemoteRelayUrl = payload.remoteRelayUrl || '';
          if (typeof payload.remoteRelaySsh === 'string') runtimeRemoteRelaySsh = payload.remoteRelaySsh || '';
          if (typeof payload.remoteRelaySshKey === 'string') runtimeRemoteRelaySshKey = payload.remoteRelaySshKey || '';
          if (typeof payload.relayProxyToken === 'string') runtimeRelayProxyToken = payload.relayProxyToken || '';
          return sendJson(response, 200, { ok: true });
        } catch (e) {
          return sendJson(response, 400, { error: 'invalid_json' });
        }
      });
      return;
    }
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