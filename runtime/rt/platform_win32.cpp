#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <cstdlib>

#include "rt/platform.hpp"

namespace rt::plat {
namespace {
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace

int open_read(const std::string& p) { return _wopen(wide(p).c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT); }
int open_write(const std::string& p) {
    return _wopen(wide(p).c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
}
int64_t pread(int fd, void* buf, size_t n, uint64_t off) {
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) return -1;
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(off);
    ov.OffsetHigh = static_cast<DWORD>(off >> 32);
    DWORD got = 0;
    const DWORD want = n > 0x7fffffff ? 0x7fffffff : static_cast<DWORD>(n);
    if (!ReadFile(h, buf, want, &got, &ov)) return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
    return got;
}

int64_t file_size(int fd) { return _filelengthi64(fd); }

bool is_dir(const std::string& p) {
    const DWORD a = GetFileAttributesW(wide(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool make_dir(const std::string& p) { return CreateDirectoryW(wide(p).c_str(), nullptr) || is_dir(p); }
bool make_dirs(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i)
        if (i == dir.size() || ((dir[i] == '/' || dir[i] == '\\') && dir[i - 1] != ':')) make_dir(dir.substr(0, i));
    return is_dir(dir);
}

int pid() { return static_cast<int>(GetCurrentProcessId()); }
long tid() { return static_cast<long>(GetCurrentThreadId()); }
std::tm local_time(std::time_t t) {
    std::tm tm{};
    localtime_s(&tm, &t);
    return tm;
}
std::string exe_path() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    std::string s = narrow(buf);
    for (char& ch : s)
        if (ch == '\\') ch = '/';
    return s;
}

void* dl_open(const char* name) { return reinterpret_cast<void*>(LoadLibraryA(name)); }
void* dl_sym(void* lib, const char* name) {
    return lib ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name)) : nullptr;
}

std::string user_dir(UserDir d) {
    const wchar_t* var = d == UserDir::State ? L"LOCALAPPDATA" : L"APPDATA";
    const wchar_t* v = _wgetenv(var);
    if (!v || !*v) return {};
    std::string s = narrow(v);
    for (char& ch : s)
        if (ch == '\\') ch = '/';
    return s;
}

}  // namespace rt::plat
#endif
