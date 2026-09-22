// platform_compat.h
//
// gedi's shared sources were written against POSIX (dirent, pwd/grp, unistd,
// popen).  This header bridges the small subset of that surface the code
// actually needs onto the Win32/MSVC C runtime so the same .cpp files build
// unmodified on Windows.  On every other platform it just includes the real
// POSIX headers, so behaviour there is unchanged.
#ifndef GEDI_PLATFORM_COMPAT_H
#define GEDI_PLATFORM_COMPAT_H

#ifdef _WIN32

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <direct.h>

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

// ---- mode_t / struct stat --------------------------------------------------
// MSVC has no mode_t and no S_ISDIR/S_ISLNK; map "stat"/"struct stat" onto the
// CRT's _stat() so the rest of the code can keep using POSIX spelling.
typedef unsigned short mode_t;
#define stat _stat

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (0)
#endif
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR _S_IEXEC
#define S_IRGRP _S_IREAD
#define S_IWGRP _S_IWRITE
#define S_IXGRP _S_IEXEC
#define S_IROTH _S_IREAD
#define S_IWOTH _S_IWRITE
#define S_IXOTH _S_IEXEC
#endif

// ---- cwd / chdir ------------------------------------------------------------
#define getcwd _getcwd
#define chdir  _chdir

// ---- popen / pclose ---------------------------------------------------------
#define popen  _popen
#define pclose _pclose

// ---- dirent.h emulation (opendir/readdir/closedir) --------------------------
struct DIR;
struct dirent {
    char d_name[PATH_MAX];
};
DIR*    opendir(const char* path);
dirent* readdir(DIR* d);
int     closedir(DIR* d);

// ---- pwd.h / grp.h stubs -----------------------------------------------------
// Windows has no per-file unix owner/group; callers already fall back to the
// numeric uid/gid (always 0 here) when these return null.
struct passwd { const char* pw_name; };
struct group  { const char* gr_name; };
inline passwd* getpwuid(int) { return nullptr; }
inline group*  getgrgid(int) { return nullptr; }

// ---- fnmatch (minimal '*'/'?' glob, case-insensitive) ------------------------
int fnmatch(const char* pattern, const char* str, int flags);

// ---- mkstemp ------------------------------------------------------------------
// tmpl must end in "XXXXXX"; on success returns an open, exclusively-created
// file descriptor (close with _close) and rewrites tmpl in place, like POSIX.
int mkstemp(char* tmpl);

// ---- current executable path (replaces readlink("/proc/self/exe", ...)) -----
// Returns the path length on success, or -1 on failure.
long win_self_exe_path(char* buf, size_t bufsize);

// ---- run a child program with redirected output, no console flash -----------
// gedi-gui has no console of its own (Windows subsystem, not Console); system()
// would spawn cmd.exe, and Windows auto-allocates a new — briefly visible —
// console for it since none is inherited. This runs exe_path directly via
// CreateProcess with CREATE_NO_WINDOW, so nothing flashes on screen; stdout
// and stderr are both redirected to output_file. Returns the exit code, or
// -1 if the process could not be started.
int run_process_captured(const std::string& exe_path, const std::string& output_file);

#else // !_WIN32

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <fnmatch.h>

#endif
#endif // GEDI_PLATFORM_COMPAT_H
