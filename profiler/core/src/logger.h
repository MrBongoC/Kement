#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>

// Minimal thread-safe logger for the Core. Writes to the file named by the
// KEMENT_LOG env var (created/truncated once), and mirrors to stdout so a
// console target shows Kement lines inline. Deliberately dependency-free.
class Logger {
public:
    static Logger& Get() {
        static Logger instance;
        return instance;
    }

    // explicitPath wins; otherwise fall back to the KEMENT_LOG env var. On
    // attach the target has no env, so the controller passes the path directly.
    void Init(const wchar_t* explicitPath = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return;
        initialized_ = true;

        wchar_t path[MAX_PATH] = {0};
        if (explicitPath != nullptr && explicitPath[0] != L'\0') {
            wcsncpy_s(path, explicitPath, _TRUNCATE);
        } else {
            DWORD n = GetEnvironmentVariableW(L"KEMENT_LOG", path, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return;
        }
        // Plain (byte) mode: we write narrow UTF-8/ASCII bytes ourselves.
        // Do NOT use ccs=UTF-8 here -- that puts the stream in Unicode mode
        // and narrow fputs would trip the CRT invalid-parameter fail-fast.
        file_ = _wfopen(path, L"wb");
    }

    void Line(const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        std::lock_guard<std::mutex> lock(mutex_);
        if (file_) {
            fputs(buf, file_);
            fputc('\n', file_);
            fflush(file_);
        }
        fprintf(stdout, "%s\n", buf);
        fflush(stdout);
    }

private:
    Logger() = default;
    ~Logger() { if (file_) fclose(file_); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    FILE* file_ = nullptr;
    bool initialized_ = false;
};

#define KLOG(...) Logger::Get().Line("[kement] " __VA_ARGS__)
