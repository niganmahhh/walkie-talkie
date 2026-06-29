const dgram = require("dgram");

const PORT = Number(process.env.PORT || 4210);
const CLIENT_TIMEOUT_MS = 30000;
const MAGIC = 0x31544b45; // "EKT1" little-endian

const server = dgram.createSocket("udp4");
const clients = new Map();

function clientKey(rinfo) {
  return `${rinfo.address}:${rinfo.port}`;
}

function cleanupClients() {
  const now = Date.now();

  for (const [key, client] of clients) {
    if (now - client.lastSeen > CLIENT_TIMEOUT_MS) {
      clients.delete(key);
      console.log(`client timeout ${key}`);
    }
  }
}

function parseHeader(message) {
  if (message.length < 14) {
    return null;
  }

  const magic = message.readUInt32LE(0);
  if (magic !== MAGIC) {
    return null;
  }

  return {
    deviceId: message.readUInt32LE(4),
    sequence: message.readUInt32LE(8),
    sampleCount: message.readUInt16LE(12),
  };
}

server.on("message", (message, rinfo) => {
  const header = parseHeader(message);
  if (!header) {
    return;
  }

  const key = clientKey(rinfo);
  const isNew = !clients.has(key);
  clients.set(key, {
    address: rinfo.address,
    port: rinfo.port,
    deviceId: header.deviceId,
    lastSeen: Date.now(),
  });

  if (isNew) {
    console.log(`client online ${key} device=${header.deviceId.toString(16)}`);
  }

  if (header.sampleCount === 0) {
    return;
  }

  for (const [otherKey, client] of clients) {
    if (otherKey === key || client.deviceId === header.deviceId) {
      continue;
    }

    server.send(message, client.port, client.address);
  }
});

server.on("listening", () => {
  const address = server.address();
  console.log(`ESP32 intercom UDP relay listening on ${address.address}:${address.port}`);
});

server.on("error", (error) => {
  console.error("relay error", error);
});

setInterval(cleanupClients, 5000).unref();

server.bind(PORT, "0.0.0.0");
