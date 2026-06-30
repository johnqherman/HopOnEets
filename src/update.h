#pragma once
// download the .eetsmod the relay points at, verify sha256, overwrite our bundle (loader re-unpacks
// on next launch). Runs on a detached worker thread.
#include "http_client.h"
#include "sha256.h"
#include "state.h"
#include <cctype>
#include <cstdio>
#ifndef _WIN32
#include <pthread.h>
#endif

enum { UPD_NONE = 0, UPD_AVAIL = 1, UPD_DOWNLOADING = 2, UPD_DONE = 3, UPD_FAILED = 4 };

struct UpdCtx {
  std::string url, sha, bundle, tmp;
};

static void *update_worker(void *arg) {
  UpdCtx *c = (UpdCtx *)arg;
  bool ok = https_download(c->url.c_str(), c->tmp.c_str(), &g_updateProgress);
  if (ok && !c->sha.empty()) { // verify before trusting a binary we're about to run
    std::string got = sha256_file(c->tmp.c_str()), want = c->sha;
    for (char &x : got) x = (char)tolower((unsigned char)x);
    for (char &x : want) x = (char)tolower((unsigned char)x);
    if (got != want) ok = false;
  }
  if (!ok) {
    remove(c->tmp.c_str());
    g_updateState = UPD_FAILED;
  } else if (rename(c->tmp.c_str(), c->bundle.c_str()) != 0) {
    remove(c->tmp.c_str());
    g_updateState = UPD_FAILED;
  } else {
    g_updateState = UPD_DONE;
  }
  delete c;
  return nullptr;
}

#ifdef _WIN32
static DWORD WINAPI update_worker_win(LPVOID p) {
  update_worker(p);
  return 0;
}
#endif

// download in the background; flips g_updateState to DONE (restart to apply) or FAILED. Idempotent.
static void update_begin() {
  if (g_updateState.load() >= UPD_DOWNLOADING)
    return; // already in flight / done
  if (g_updateUrl.empty())
    return;
  g_updateState = UPD_DOWNLOADING;
  UpdCtx *c = new UpdCtx();
  c->url = g_updateUrl;
  c->sha = g_updateSha;
  c->bundle = Eets::ModBundlePath(MOD); // pointer valid until next call -> copy now
  c->tmp = c->bundle + ".new";
#ifdef _WIN32
  HANDLE h = CreateThread(nullptr, 0, update_worker_win, c, 0, nullptr);
  if (h) CloseHandle(h);
  else update_worker(c); // fallback: run inline (blocks once) rather than leak the update
#else
  pthread_t t;
  if (pthread_create(&t, nullptr, update_worker, c) == 0) pthread_detach(t);
  else update_worker(c);
#endif
}

// relaunch the game so the loader re-unpacks the new bundle. Best-effort.
static void restart_game() {
#ifdef _WIN32
  wchar_t path[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
  STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
  PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
  std::wstring cmd = GetCommandLineW(); // mutable copy for CreateProcessW
  if (CreateProcessW(path, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(0);
  }
#else
  char exe[4096];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) return;
  exe[n] = 0;
  // rebuild argv from /proc/self/cmdline; env (incl. LD_PRELOAD) is inherited across execv
  std::vector<std::string> args;
  if (FILE *f = fopen("/proc/self/cmdline", "rb")) {
    std::string cur; int ch;
    while ((ch = fgetc(f)) != EOF) {
      if (ch == 0) { args.push_back(cur); cur.clear(); }
      else cur += (char)ch;
    }
    if (!cur.empty()) args.push_back(cur);
    fclose(f);
  }
  if (args.empty()) args.push_back(exe);
  std::vector<char *> argv;
  for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  execv(exe, argv.data()); // replaces this process image; returns only on failure
#endif
}
