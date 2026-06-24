// ws_client.h - WebSocket client transport for the mod. The mod connects DIRECTLY to the relay over
// ws:// or wss:// (no bridge): this layer does DNS + TCP + (for wss) TLS via the system OpenSSL loaded at
// runtime with dlopen (no hard link dependency, graceful if absent) + the WebSocket handshake and masked
// text framing. The relay speaks the mod's text protocol over WS frames (one line per frame), so net.h
// keeps its text parser unchanged. Linux is implemented; Windows is a stub (online disabled there for now
// - the native WinHTTP WebSocket port is future work, like the other deferred Win pieces).
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// parsed ws/wss URL
struct WscUrl { bool tls = false; std::string host; int port = 0; std::string path = "/"; bool ok = false; };
[[maybe_unused]] static WscUrl wsc_parse(const std::string& url) {
	WscUrl u; std::string s = url;
	if      (s.rfind("wss://", 0) == 0) { u.tls = true;  s = s.substr(6); }
	else if (s.rfind("ws://",  0) == 0) { u.tls = false; s = s.substr(5); }
	else return u;
	size_t slash = s.find('/');
	std::string hp = (slash == std::string::npos) ? s : s.substr(0, slash);
	if (slash != std::string::npos) u.path = s.substr(slash);
	if (u.path.empty()) u.path = "/";
	size_t colon = hp.find(':');
	if (colon != std::string::npos) { u.host = hp.substr(0, colon); u.port = atoi(hp.c_str() + colon + 1); }
	else { u.host = hp; u.port = u.tls ? 443 : 80; }
	u.ok = !u.host.empty() && u.port > 0;
	return u;
}

[[maybe_unused]] static std::string wsc_b64(const unsigned char* d, int n) {
	static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string o; int i = 0;
	while (i < n) {
		int v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		o.push_back(T[(v >> 18) & 63]); o.push_back(T[(v >> 12) & 63]);
		o.push_back(i + 1 < n ? T[(v >> 6) & 63] : '='); o.push_back(i + 2 < n ? T[v & 63] : '='); i += 3;
	}
	return o;
}

#ifdef _WIN32
// ---- Windows: WinHTTP WebSocket (native TLS + framing). Receive blocks, so it runs on a Win32 thread
// feeding a CRITICAL_SECTION-guarded inbox that wsc_poll drains. Win32 threads (not std::thread) avoid the
// mingw threading-model dependency. Cross-compiles with mingw; UNVERIFIED at runtime (no Win test yet). ----
#include <windows.h>
#include <winhttp.h>
#include <vector>

static HINTERNET g_whSession = nullptr, g_whConn = nullptr, g_whWs = nullptr;
static HANDLE g_whThread = nullptr;
static CRITICAL_SECTION g_whCs;
static bool g_whCsInit = false;
static std::vector<std::string> g_whInbox;
static volatile bool g_whRun = false, g_whOpen = false;

static void wsc_close() {
	g_whRun = false;
	if (g_whWs) WinHttpWebSocketClose(g_whWs, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);   // unblocks the recv thread
	if (g_whThread) { WaitForSingleObject(g_whThread, 3000); CloseHandle(g_whThread); g_whThread = nullptr; }
	if (g_whWs)      { WinHttpCloseHandle(g_whWs); g_whWs = nullptr; }
	if (g_whConn)    { WinHttpCloseHandle(g_whConn); g_whConn = nullptr; }
	if (g_whSession) { WinHttpCloseHandle(g_whSession); g_whSession = nullptr; }
	g_whOpen = false;
	if (g_whCsInit) { EnterCriticalSection(&g_whCs); g_whInbox.clear(); LeaveCriticalSection(&g_whCs); }
}
static bool wsc_up() { return g_whOpen; }

static DWORD WINAPI wh_recv_loop(LPVOID) {
	std::string acc; BYTE buf[4096];
	while (g_whRun) {
		DWORD got = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
		if (WinHttpWebSocketReceive(g_whWs, buf, sizeof(buf), &got, &type) != NO_ERROR) break;
		if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
		acc.append((const char*)buf, got);
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
			EnterCriticalSection(&g_whCs); g_whInbox.push_back(acc); LeaveCriticalSection(&g_whCs);
			acc.clear();
		}   // *_FRAGMENT types: keep accumulating until the terminating message buffer
	}
	g_whOpen = false;
	return 0;
}

static bool wsc_connect(const std::string& url) {
	wsc_close();
	WscUrl u = wsc_parse(url); if (!u.ok) return false;
	if (!g_whCsInit) { InitializeCriticalSection(&g_whCs); g_whCsInit = true; }
	std::wstring hostW(u.host.begin(), u.host.end()), pathW(u.path.begin(), u.path.end());
	g_whSession = WinHttpOpen(L"HopOnEets", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!g_whSession) return false;
	g_whConn = WinHttpConnect(g_whSession, hostW.c_str(), (INTERNET_PORT)u.port, 0);
	if (!g_whConn) { wsc_close(); return false; }
	HINTERNET req = WinHttpOpenRequest(g_whConn, L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER,
	                                   WINHTTP_DEFAULT_ACCEPT_TYPES, u.tls ? WINHTTP_FLAG_SECURE : 0);
	if (!req) { wsc_close(); return false; }
	bool ok = WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
	       && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
	       && WinHttpReceiveResponse(req, nullptr);
	if (ok) g_whWs = WinHttpWebSocketCompleteUpgrade(req, 0);
	WinHttpCloseHandle(req);
	if (!g_whWs) { wsc_close(); return false; }
	g_whOpen = true; g_whRun = true;
	g_whThread = CreateThread(nullptr, 0, wh_recv_loop, nullptr, 0, nullptr);
	return true;
}

static void wsc_send_text(const std::string& s) {
	if (g_whOpen) WinHttpWebSocketSend(g_whWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)s.data(), (DWORD)s.size());
}

static void wsc_poll(void (*onmsg)(const std::string&)) {
	if (!g_whCsInit) return;
	std::vector<std::string> batch;
	EnterCriticalSection(&g_whCs); batch.swap(g_whInbox); LeaveCriticalSection(&g_whCs);
	for (auto& m : batch) onmsg(m);
}
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <ctime>

// minimal runtime-loaded OpenSSL (only the calls we need). cert verification is intentionally skipped
// (informal ladder); SNI is set so name-based vhosts / proxies route correctly.
struct WscSSL {
	void* lib = nullptr;
	const void* (*client_method)() = nullptr;
	void* (*CTX_new)(const void*) = nullptr;
	void  (*CTX_free)(void*) = nullptr;
	void* (*SSL_new)(void*) = nullptr;
	int   (*set_fd)(void*, int) = nullptr;
	long  (*ctrl)(void*, int, long, void*) = nullptr;
	int   (*connect)(void*) = nullptr;
	int   (*read)(void*, void*, int) = nullptr;
	int   (*write)(void*, const void*, int) = nullptr;
	int   (*get_error)(const void*, int) = nullptr;
	void  (*SSL_free)(void*) = nullptr;
	bool  ok = false;
};
static WscSSL& wsc_ssl() {
	static WscSSL s; static bool tried = false;
	if (tried) return s;
	tried = true;
	const char* names[] = { "libssl.so.3", "libssl.so.1.1", "libssl.so" };
	for (const char* n : names) { s.lib = dlopen(n, RTLD_NOW | RTLD_GLOBAL); if (s.lib) break; }
	if (!s.lib) return s;
	auto L = [&](const char* sym) { return dlsym(s.lib, sym); };
	s.client_method = (const void*(*)())L("TLS_client_method");
	s.CTX_new  = (void*(*)(const void*))L("SSL_CTX_new");
	s.CTX_free = (void(*)(void*))L("SSL_CTX_free");
	s.SSL_new  = (void*(*)(void*))L("SSL_new");
	s.set_fd   = (int(*)(void*, int))L("SSL_set_fd");
	s.ctrl     = (long(*)(void*, int, long, void*))L("SSL_ctrl");
	s.connect  = (int(*)(void*))L("SSL_connect");
	s.read     = (int(*)(void*, void*, int))L("SSL_read");
	s.write    = (int(*)(void*, const void*, int))L("SSL_write");
	s.get_error= (int(*)(const void*, int))L("SSL_get_error");
	s.SSL_free = (void(*)(void*))L("SSL_free");
	s.ok = s.client_method && s.CTX_new && s.SSL_new && s.set_fd && s.connect && s.read && s.write && s.get_error;
	return s;
}

static int   g_wsFd  = -1;
static bool  g_wsTls = false;
static void* g_wsCtx = nullptr;
static void* g_wsSsl = nullptr;
static bool  g_wsOpen = false;
static std::string g_wsRx;

static int wsc_raw_write(const void* p, int n) { return g_wsTls ? wsc_ssl().write(g_wsSsl, p, n) : (int)::send(g_wsFd, p, n, MSG_NOSIGNAL); }
static int wsc_raw_read(void* p, int n)        { return g_wsTls ? wsc_ssl().read(g_wsSsl, p, n)  : (int)::recv(g_wsFd, p, n, 0); }
// write all bytes, retrying transient non-blocking would-block (TLS WANT_READ/WRITE, EAGAIN) briefly
// instead of treating it as a hard failure (which would spuriously drop the connection). Small control
// frames almost always go in one write; the spin cap bounds a genuinely stuck socket.
static bool wsc_write_all(const char* p, size_t n) {
	size_t o = 0; int spins = 0;
	while (o < n) {
		int w = wsc_raw_write(p + o, (int)(n - o));
		if (w > 0) { o += (size_t)w; spins = 0; continue; }
		if (++spins > 2000) return false;   // ~0.4s of would-block = treat as dead
		usleep(200);
	}
	return true;
}

static void wsc_close() {
	if (g_wsSsl) { wsc_ssl().SSL_free(g_wsSsl); g_wsSsl = nullptr; }
	if (g_wsCtx) { wsc_ssl().CTX_free(g_wsCtx); g_wsCtx = nullptr; }
	if (g_wsFd >= 0) { ::close(g_wsFd); g_wsFd = -1; }
	g_wsOpen = false; g_wsTls = false; g_wsRx.clear();
}
static bool wsc_up() { return g_wsOpen; }

// build + send one masked frame (client frames MUST be masked per RFC 6455)
static void wsc_send_frame(unsigned char opcode, const std::string& s) {
	if (!g_wsOpen) return;
	std::string f; size_t n = s.size();
	f.push_back((char)(0x80 | opcode));   // FIN + opcode
	if (n < 126) f.push_back((char)(0x80 | n));
	else if (n < 65536) { f.push_back((char)(0x80 | 126)); f.push_back((char)(n >> 8)); f.push_back((char)(n & 0xff)); }
	else { f.push_back((char)(0x80 | 127)); for (int i = 7; i >= 0; i--) f.push_back((char)((n >> (8 * i)) & 0xff)); }
	unsigned char key[4]; for (int i = 0; i < 4; i++) key[i] = (unsigned char)(rand() & 0xff);
	f.append((const char*)key, 4);
	for (size_t i = 0; i < n; i++) f.push_back((char)((unsigned char)s[i] ^ key[i & 3]));
	if (!wsc_write_all(f.data(), f.size())) wsc_close();
}
static void wsc_send_text(const std::string& s) { wsc_send_frame(0x1, s); }

// connect: DNS + TCP + (wss) TLS + WS handshake. Blocking with a short timeout (called on a menu action),
// then the fd is switched to non-blocking for wsc_poll.
static bool wsc_connect(const std::string& url) {
	wsc_close();
	WscUrl u = wsc_parse(url); if (!u.ok) return false;
	g_wsTls = u.tls;
	if (g_wsTls && !wsc_ssl().ok) return false;   // no OpenSSL available -> can't do wss

	char ports[16]; snprintf(ports, sizeof(ports), "%d", u.port);
	addrinfo hints; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	addrinfo* res = nullptr;
	if (getaddrinfo(u.host.c_str(), ports, &hints, &res) != 0 || !res) return false;
	int fd = -1;
	for (addrinfo* ai = res; ai; ai = ai->ai_next) {
		fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol); if (fd < 0) continue;
		timeval tv{ 6, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
		::close(fd); fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) return false;
	g_wsFd = fd;

	if (g_wsTls) {
		WscSSL& o = wsc_ssl();
		g_wsCtx = o.CTX_new(o.client_method()); if (!g_wsCtx) { wsc_close(); return false; }
		g_wsSsl = o.SSL_new(g_wsCtx); if (!g_wsSsl) { wsc_close(); return false; }
		o.set_fd(g_wsSsl, fd);
		o.ctrl(g_wsSsl, 55 /*SSL_CTRL_SET_TLSEXT_HOSTNAME*/, 0 /*TLSEXT_NAMETYPE_host_name*/, (void*)u.host.c_str());
		if (o.connect(g_wsSsl) != 1) { wsc_close(); return false; }
	}

	// WebSocket upgrade request
	unsigned char rnd[16]; for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xff);
	std::string key = wsc_b64(rnd, 16);
	char req[512];
	int rn = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", u.path.c_str(), u.host.c_str(), key.c_str());
	if (!wsc_write_all(req, (size_t)rn)) { wsc_close(); return false; }

	// read the handshake response up to the blank line; accept on a "101" status
	std::string resp; char buf[512];
	for (int tries = 0; tries < 64 && resp.find("\r\n\r\n") == std::string::npos; tries++) {
		int n = wsc_raw_read(buf, sizeof(buf)); if (n <= 0) break; resp.append(buf, (size_t)n);
	}
	if (resp.find(" 101") == std::string::npos) { wsc_close(); return false; }

	int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);   // non-blocking for polling
	g_wsOpen = true;
	// any bytes past the handshake are the first frames
	size_t hdr = resp.find("\r\n\r\n"); if (hdr != std::string::npos && hdr + 4 < resp.size()) g_wsRx.assign(resp, hdr + 4, std::string::npos);
	return true;
}

// drain available frames, delivering each text message to onmsg. Replies to pings; closes on a close frame.
static void wsc_poll(void (*onmsg)(const std::string&)) {
	if (!g_wsOpen) return;
	char buf[4096]; int n;
	while ((n = wsc_raw_read(buf, sizeof(buf))) > 0) g_wsRx.append(buf, (size_t)n);
	if (n == 0 && !g_wsTls) { wsc_close(); return; }   // plain TCP peer closed
	// Parse ALL complete frames into local lists and consume them from g_wsRx FIRST, then dispatch. A
	// handler (onmsg) may send "ready" etc. and even close the connection (clearing g_wsRx) - doing that
	// mid-parse previously underflowed `size - off` and read out of bounds. Parse-then-dispatch is safe.
	std::vector<std::string> msgs, pongs; bool gotClose = false;
	size_t off = 0, sz = g_wsRx.size();
	while (off + 2 <= sz) {
		unsigned char b0 = (unsigned char)g_wsRx[off], b1 = (unsigned char)g_wsRx[off + 1];
		int op = b0 & 0x0f; bool masked = (b1 & 0x80) != 0; uint64_t len = b1 & 0x7f; size_t hl = 2;
		if (len == 126) { if (off + 4 > sz) break; len = ((unsigned char)g_wsRx[off + 2] << 8) | (unsigned char)g_wsRx[off + 3]; hl = 4; }
		else if (len == 127) { if (off + 10 > sz) break; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | (unsigned char)g_wsRx[off + 2 + i]; hl = 10; }
		size_t mk = masked ? 4 : 0;
		if (off + hl + mk + len > sz) break;   // frame incomplete - wait for more
		std::string payload(g_wsRx.data() + off + hl + mk, (size_t)len);   // server frames are unmasked
		off += hl + mk + (size_t)len;
		if (op == 0x1 || op == 0x0) msgs.push_back(std::move(payload));
		else if (op == 0x9) pongs.push_back(std::move(payload));   // ping
		else if (op == 0x8) { gotClose = true; break; }            // close
	}
	if (off) g_wsRx.erase(0, off);   // consume parsed bytes before any handler runs
	for (auto& p : pongs) { if (!g_wsOpen) break; wsc_send_frame(0xA, p); }
	if (gotClose) { wsc_close(); return; }
	for (auto& m : msgs) { if (!g_wsOpen) break; onmsg(m); }   // onmsg may close the socket; stop if so
}
#endif
