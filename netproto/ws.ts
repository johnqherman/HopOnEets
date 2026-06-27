import * as crypto from "crypto";
import * as net from "net";
import type { IncomingMessage } from "http";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

export function acceptKey(key: string): string {
  return crypto
    .createHash("sha1")
    .update(key + GUID)
    .digest("base64");
}

export function encodeFrame(str: string, mask: boolean): Buffer {
  const payload = Buffer.from(str, "utf8"),
    len = payload.length;
  let header: Buffer;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  header[0] = 0x81; // FIN + text
  if (!mask) return Buffer.concat([header, payload]);
  header[1] |= 0x80;
  const mk = crypto.randomBytes(4),
    out = Buffer.alloc(len);
  for (let i = 0; i < len; i++) out[i] = payload[i] ^ mk[i & 3];
  return Buffer.concat([header, mk, out]);
}

export type Parser = (chunk: Buffer) => void;

export function createParser(
  onMsg: (s: string) => void,
  onClose?: () => void,
): Parser {
  let buf = Buffer.alloc(0);
  return (chunk: Buffer) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 2) return;
      const op = buf[0] & 0x0f,
        masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f,
        off = 2;
      if (len === 126) {
        if (buf.length < 4) return;
        len = buf.readUInt16BE(2);
        off = 4;
      } else if (len === 127) {
        if (buf.length < 10) return;
        len = Number(buf.readBigUInt64BE(2));
        off = 10;
      }
      let mk: Buffer | null = null;
      if (masked) {
        if (buf.length < off + 4) return;
        mk = buf.subarray(off, off + 4);
        off += 4;
      }
      if (buf.length < off + len) return;
      let payload = buf.subarray(off, off + len);
      if (masked && mk) {
        const u = Buffer.alloc(len);
        for (let i = 0; i < len; i++) u[i] = payload[i] ^ mk[i & 3];
        payload = u;
      }
      buf = buf.subarray(off + len);
      if (op === 0x8) {
        onClose?.();
        return;
      }
      if (op === 0x1 || op === 0x0) onMsg(payload.toString("utf8"));
      // 0x9 ping / 0xA pong ignored
    }
  };
}

export interface WSConn {
  send(obj: unknown): void; // JSON frame
  sendText(s: string): void; // raw text frame (mod protocol)
  ping(): void; // WS ping frame (keepalive; peer auto-pongs)
  onJSON(fn: (m: any) => void): void;
  onText(fn: (s: string) => void): void; // raw frames; if set, bypasses JSON path
  onClose(fn: () => void): void;
  close(): void;
}

// server: handle http 'upgrade'
export function accept(req: IncomingMessage, socket: net.Socket): WSConn {
  const k = req.headers["sec-websocket-key"] as string;
  socket.write(
    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
      "Sec-WebSocket-Accept: " +
      acceptKey(k) +
      "\r\n\r\n",
  );
  return wrap(socket, false);
}

// client: connect to ws://host:port, cb(conn) once upgraded
export function connect(
  host: string,
  port: number,
  cb: (conn: WSConn | null, err?: Error) => void,
): { conn: WSConn | null; socket: net.Socket } {
  const key = crypto.randomBytes(16).toString("base64");
  const socket = net.connect(port, host, () => {
    socket.write(
      `GET / HTTP/1.1\r\nHost: ${host}:${port}\r\nUpgrade: websocket\r\n` +
        `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
    );
  });
  let upgraded = false,
    pending = Buffer.alloc(0);
  let conn: WSConn | null = null;
  socket.on("data", (chunk: Buffer) => {
    if (upgraded) return;
    pending = Buffer.concat([pending, chunk]);
    const i = pending.indexOf("\r\n\r\n");
    if (i < 0) return;
    upgraded = true;
    conn = wrap(socket, true);
    cb(conn);
    const rest = pending.subarray(i + 4);
    if (rest.length) socket.emit("data", rest);
  });
  socket.on("error", (e: Error) => {
    if (!upgraded) cb(null, e);
  });
  return {
    get conn() {
      return conn;
    },
    socket,
  };
}

function wrap(socket: net.Socket, mask: boolean): WSConn {
  let onJSON: (m: any) => void = () => {};
  let onText: ((s: string) => void) | null = null;
  let onClose: () => void = () => {};
  const parser = createParser(
    (s) => {
      if (onText) {
        onText(s);
        return;
      }
      let m: unknown;
      try {
        m = JSON.parse(s);
      } catch {
        return;
      }
      onJSON(m);
    },
    () => onClose(),
  );
  let closed = false;
  const fireClose = () => {
    if (closed) return;
    closed = true;
    onClose();
  };
  socket.on("data", parser);
  socket.on("end", () => {
    try {
      socket.end();
    } catch {
      /* */
    }
    fireClose();
  }); // peer FIN (upgraded sockets may allowHalfOpen)
  socket.on("close", () => fireClose());
  socket.on("error", () => {});
  return {
    send: (obj: unknown) => {
      try {
        socket.write(encodeFrame(JSON.stringify(obj), mask));
      } catch {
        /* closed */
      }
    },
    sendText: (s: string) => {
      try {
        socket.write(encodeFrame(s, mask));
      } catch {
        /* closed */
      }
    },
    ping: () => {
      try {
        socket.write(Buffer.from([0x89, 0x00])); // FIN + ping opcode, no payload (server frames unmasked)
      } catch {
        /* closed */
      }
    },
    onJSON: (fn) => {
      onJSON = fn;
    },
    onText: (fn) => {
      onText = fn;
    },
    onClose: (fn) => {
      onClose = fn;
    },
    close: () => {
      try {
        socket.end();
      } catch {
        /* closed */
      }
    },
  };
}
