// net.h - realtime net client. mod <-> localhost TCP <-> bridge <-> WebSocket <-> relay.
// POSIX sockets (zero link deps); Windows online is a follow-up winsock build. Two ways in:
// host/join by code, or ranked matchmaking. See docs/net-protocol.md.
#pragma once
#include "state.h"

#ifndef _WIN32
static int g_sock = -1;
static std::string g_rx;
static void net_sendline(const std::string& s) {
	if (g_sock < 0) return;
	std::string l = s + "\n";
	(void)::send(g_sock, l.data(), l.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}
static void net_close() { if (g_sock >= 0) ::close(g_sock); g_sock = -1; g_matched = false; g_liveValid = false; }
static bool net_connect() {
	g_sock = ::socket(AF_INET, SOCK_STREAM, 0);
	if (g_sock < 0) return false;
	sockaddr_in a; memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET; a.sin_port = htons((uint16_t)g_bridgePort);
	if (inet_pton(AF_INET, g_bridgeHost.c_str(), &a.sin_addr) != 1 || ::connect(g_sock, (sockaddr*)&a, sizeof(a)) < 0) {
		::close(g_sock); g_sock = -1; return false;
	}
	int fl = fcntl(g_sock, F_GETFL, 0); fcntl(g_sock, F_SETFL, fl | O_NONBLOCK);
	net_sendline("hello " + g_playerId);
	return true;
}
static void net_handle(const std::string& ln) {
	char a[40] = { 0 }, b[40] = { 0 }; long t; float x, y; int rk = 0, iv = 0;
	if (sscanf(ln.c_str(), "g %ld %f %f", &t, &x, &y) == 3) { (void)t; if (valid_pos(x, y)) { g_liveX = x; g_liveY = y; g_liveValid = true; } }
	else if (sscanf(ln.c_str(), "ob %39s %f %f", a, &x, &y) == 3) { if (valid_pos(x, y)) { if (g_oppBuildReady) { g_oppBuild.clear(); g_oppBuildReady = false; } g_oppBuild.push_back({ a, x, y }); } }
	else if (strncmp(ln.c_str(), "obend", 5) == 0) g_oppBuildReady = true;
	else if (sscanf(ln.c_str(), "match %39s %39s %d", a, b, &rk) >= 2) {
		g_matched = true; g_ranked = rk != 0; g_oppId = b; g_liveValid = false; g_oppBuild.clear(); g_oppBuildReady = false;
		snprintf(g_netMsg, sizeof(g_netMsg), "matched vs %s%s", b, g_ranked ? " [ranked]" : "");
	} else if (sscanf(ln.c_str(), "countdown %d", &iv) == 1) {
		if (iv > 0) g_buildSeconds = iv;      // synced build phase: align both clients to the relay signal
		g_buildStart = Time(); g_forcedStart = false;
		snprintf(g_netMsg, sizeof(g_netMsg), "build %ds (synced)", g_buildSeconds);
	} else if (sscanf(ln.c_str(), "code %39s", a) == 1) snprintf(g_netMsg, sizeof(g_netMsg), "hosting - code %s", a);
	else if (sscanf(ln.c_str(), "joinfail %39s", a) == 1) snprintf(g_netMsg, sizeof(g_netMsg), "no game for code %s", a);
	else if (strncmp(ln.c_str(), "result ", 7) == 0) {
		char w[16] = { 0 }, r[24] = { 0 }; sscanf(ln.c_str() + 7, "%15s %23s", w, r);
		if (!strcmp(w, "you")) g_youWins++; else if (!strcmp(w, "opponent")) g_ghostWins++;
		snprintf(g_roundMsg, sizeof(g_roundMsg), "ONLINE result: %s by %s  match %d-%d", w, r, g_youWins, g_ghostWins);
	} else if (strncmp(ln.c_str(), "oppleft", 7) == 0) { g_matched = false; g_liveValid = false; snprintf(g_netMsg, sizeof(g_netMsg), "opponent left"); }
}
static void net_poll() {
	if (g_sock < 0) return;
	char buf[1024]; ssize_t n;
	while ((n = ::recv(g_sock, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
		g_rx.append(buf, (size_t)n);
		size_t p; while ((p = g_rx.find('\n')) != std::string::npos) { net_handle(g_rx.substr(0, p)); g_rx.erase(0, p + 1); }
	}
}
static bool net_up() { return g_sock >= 0; }
#else
static void net_sendline(const std::string&) {}
static void net_close() {}
static bool net_connect() { static bool warned = false; if (!warned) { warned = true; Eets::Log("hop_on_eets: online needs the winsock build on Windows (TODO) - Linux for now"); } return false; }
static void net_poll() {}
static bool net_up() { return false; }
#endif

// connect on demand, then send a command line to the bridge
static void net_action(const std::string& cmd) {
	if (!net_up()) { if (!net_connect()) { snprintf(g_netMsg, sizeof(g_netMsg), "bridge offline (%s:%d)", g_bridgeHost.c_str(), g_bridgePort); return; } }
	net_sendline(cmd);
}
