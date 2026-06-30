#pragma once
// HTTPS GET to a file, following redirects (GitHub -> CDN). *prog = 0..100 (-1 unknown). true on 2xx.
#include <atomic>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <cstdio>

static std::wstring http_widen(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
  return w;
}

static bool https_download(const char *url, const char *outPath, std::atomic<int> *prog = nullptr) {
  if (prog) *prog = -1;
  std::wstring wurl = http_widen(url);
  URL_COMPONENTS uc; ZeroMemory(&uc, sizeof(uc)); uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {0}, path[2048] = {0};
  uc.lpszHostName = host; uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
  if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false; // https only

  bool ok = false;
  HINTERNET hS = WinHttpOpen(L"hop_on_eets-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (hS) {
    HINTERNET hC = WinHttpConnect(hS, host, uc.nPort, 0);
    if (hC) {
      HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if (hR) {
        if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hR, nullptr)) {
          DWORD code = 0, len = sizeof(code);
          WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
          unsigned long long total = 0; DWORD tl = sizeof(total);
          WinHttpQueryHeaders(hR, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                              WINHTTP_HEADER_NAME_BY_INDEX, &total, &tl, WINHTTP_NO_HEADER_INDEX);
          if (code >= 200 && code < 300) {
            FILE *f = fopen(outPath, "wb");
            if (f) {
              ok = true; unsigned long long done = 0;
              for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hR, &avail)) { ok = false; break; }
                if (avail == 0) break;
                while (avail > 0) {
                  char buf[16384];
                  DWORD want = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), got = 0;
                  if (!WinHttpReadData(hR, buf, want, &got) || got == 0) { ok = false; break; }
                  fwrite(buf, 1, got, f);
                  done += got; avail -= got;
                  if (prog && total > 0) { int p = (int)(done * 100 / total); *prog = p < 99 ? p : 99; }
                }
                if (!ok) break;
              }
              fclose(f);
            }
          }
        }
        WinHttpCloseHandle(hR);
      }
      WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
  }
  if (ok && prog) *prog = 100;
  return ok;
}

#else // curl via fork+exec (no shell = no URL injection)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <vector>

// total size via HEAD (last Content-Length after redirects); 0 = unknown
static long long http_head_length(const char *url) {
  int fd[2];
  if (pipe(fd) != 0) return 0;
  pid_t pid = fork();
  if (pid < 0) { close(fd[0]); close(fd[1]); return 0; }
  if (pid == 0) {
    dup2(fd[1], 1); close(fd[0]); close(fd[1]);
    execlp("curl", "curl", "-sIL", "--proto", "=https", "--max-time", "20", url, (char *)nullptr);
    _exit(127);
  }
  close(fd[1]);
  std::string out; char buf[4096]; ssize_t n;
  while ((n = read(fd[0], buf, sizeof(buf))) > 0) out.append(buf, n);
  close(fd[0]);
  int st = 0; waitpid(pid, &st, 0);
  long long total = 0;
  for (size_t i = 0; i < out.size();) {
    size_t e = out.find('\n', i); if (e == std::string::npos) e = out.size();
    std::string line = out.substr(i, e - i);
    std::string low = line; for (char &c : low) c = (char)tolower((unsigned char)c);
    if (low.rfind("content-length:", 0) == 0) total = atoll(line.c_str() + 15);
    i = e + 1;
  }
  return total > 0 ? total : 0;
}

static long long file_size(const char *p) { struct stat s; return stat(p, &s) == 0 ? (long long)s.st_size : 0; }

static bool https_download(const char *url, const char *outPath, std::atomic<int> *prog = nullptr) {
  if (prog) *prog = -1;
  if (!url || strncmp(url, "https://", 8) != 0) return false; // https only
  long long total = prog ? http_head_length(url) : 0;
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execlp("curl", "curl", "-fsSL", "--proto", "=https", "--max-time", "120",
           "-o", outPath, url, (char *)nullptr);
    _exit(127);
  }
  // poll the growing temp file for a percentage while curl runs
  int st = 0;
  while (waitpid(pid, &st, WNOHANG) == 0) {
    if (prog && total > 0) { int p = (int)(file_size(outPath) * 100 / total); *prog = p < 99 ? p : 99; }
    usleep(100000);
  }
  bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
  if (ok && prog) *prog = 100;
  return ok;
}
#endif
