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

#include <cstdio>
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

// ---- popen / pclose / shell -------------------------------------------------
// Not _popen: it hands the command to cmd.exe with no say over how the child is
// created, and a Windows-subsystem process has no console of its own to lend
// it, so Windows allocates a fresh - and visible - one for the shell. Every
// pipe the editor opens would flash a command prompt on screen, and a slow one
// would leave it sitting there: the librarian.py library scan started at
// startup is seconds of visible console on a GUI app that has not even finished
// drawing. These route the shell through CreateProcess with CREATE_NO_WINDOW
// instead: the child gets a console, so cmd.exe and the console tools behind it
// behave exactly as before - it simply has no window. Note that detaching the
// child instead (DETACHED_PROCESS) sounds safer and is worse: a console program
// given no console allocates one as soon as it wants one, and cmd.exe does, so
// the command ends up with a fresh console that the default terminal
// application puts on screen. A windowless console leaves it nothing to ask
// for.
//
// The command goes to "cmd.exe /S /C" wrapped in one pair of quotes. /S makes
// cmd strip precisely the first and last quote and take everything between them
// verbatim, which leaves a command starting with a quoted compiler path intact.
// Without /S cmd applies its own rule - strip the leading quote and the last
// quote on the line - and the command arrives mangled, the child dying with
// "The filename, directory name, or volume label syntax is incorrect".
//
// mode is "r" (read the child's stdout) or "w" (write to its stdin), with the
// usual "b"/"t" suffix honoured; text mode is the default, as with _popen. The
// standard handle the mode does not use is the null device: a tool that
// unexpectedly asks something then reads end-of-file instead of wedging the
// editor, and stderr goes nowhere unless the command redirects it - which is
// what the "2>&1" and "2>NUL" the call sites already append is for.
FILE* gedi_popen(const std::string& command, const char* mode);

// Closes the stream, waits for the child and returns its exit code, like
// _pclose. Returns -1 if the stream did not come from gedi_popen.
int   gedi_pclose(FILE* stream);

// The same shell, equally invisible, for a command whose output nobody reads:
// all three standard handles point at the null device and the exit code is
// returned. Stands in for system(), which would pop up a console window for
// exactly the reason above.
int   gedi_shell(const std::string& command);

// MinGW's <stdio.h> already maps popen onto _popen; replace that mapping rather
// than redefining the macro on top of it.
#undef popen
#undef pclose
#define popen  gedi_popen
#define pclose gedi_pclose

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
// would spawn cmd.exe, and Windows allocates a new - and visible - console for
// it since none is inherited. This runs exe_path directly via CreateProcess with
// CREATE_NO_WINDOW, so nothing appears on screen; stdout and stderr are both
// redirected to output_file. Returns the exit code, or -1 if the process could
// not be started.
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
