const { spawn } = require('child_process');
const path = require('path');
const express = require('express');
const fs = require('fs');
const app = express();

const PORT = 4200;
app.set('view engine', null);

const execDir = path.dirname(process.execPath);
const distParts = ['frontend', 'dist', 'frontend', 'browser'];

// __dirname inside pkg = snapshot root, check that first
let distPath = path.join(__dirname, ...distParts);

const indexInSnapshot = path.join(distPath, 'index.html');
const indexExists = (() => {
    try { fs.readFileSync(indexInSnapshot); return true; }
    catch { return false; }
})();

if (!indexExists) {
    distPath = path.join(execDir, ...distParts);
}

console.log(`📂 Serving Frontend from: ${distPath}`);

// grpcwebproxy always lives on the real filesystem next to the binary
const proxyPath = path.join(execDir, 'grpcwebproxy');
console.log(`🚀 Starting gRPC Proxy at: ${proxyPath}`);

const proxy = spawn(proxyPath, [
    '--backend_addr=localhost:50051',
    '--server_http_debug_port=8080',
    '--run_tls_server=false',
    '--allow_all_origins'
], { shell: true, cwd: execDir });

proxy.stdout.on('data', (data) => console.log(`[Proxy]: ${data}`));
proxy.stderr.on('data', (data) => console.error(`[Proxy Message]: ${data}`));

app.use(express.static(distPath));

app.use((req, res, next) => {
    if (req.method === 'GET') {
        const indexPath = path.join(distPath, 'index.html');
        try {
            const html = fs.readFileSync(indexPath);
            res.setHeader('Content-Type', 'text/html');
            return res.end(html);
        } catch (e) {
            return res.status(404).send(`index.html not found at: ${indexPath}\n${e.message}`);
        }
    }
    next();
});

app.listen(PORT, () => {
    console.log(`🌐 Frontend available at http://localhost:${PORT}`);
});

process.on('SIGINT', () => {
    proxy.kill();
    process.exit();
});