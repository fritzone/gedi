#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include "Renderer.h"
#include <string>
#include <vector>
#include <sys/stat.h>

// ─────────────────────────────────────────────────────────────────────────────
struct FileEntry {
    std::string name;
    bool        is_directory = false;
    off_t       size         = 0;
    time_t      mod_time     = 0;
    mode_t      permissions  = 0;
    std::string owner;
    std::string group;
};

// ─────────────────────────────────────────────────────────────────────────────
// FileBrowser — three modal browser dialogs sharing one implementation.
//
//   open()            — select an existing file; returns full path or ""
//   save()            — choose a filename to save to; returns full path or ""
//   selectDirectory() — select a directory; returns full path or ""
// ─────────────────────────────────────────────────────────────────────────────
class FileBrowser {
public:
    static std::string open           (Renderer& renderer);
    static std::string save           (Renderer& renderer,
                            const std::string& current_filename = "");
    static std::string selectDirectory(Renderer& renderer);

private:
    enum class Mode { OPEN, SAVE, SELECT_DIR };

    // All three public methods delegate here.
    static std::string run(Renderer& renderer, Mode mode,
                           const std::string& initial_filename);

    // ── Directory helpers ─────────────────────────────────────────────────────
    static std::vector<FileEntry> readDirectory(const std::string& path);
    static void sortEntries(std::vector<FileEntry>& entries);

    // ── Drawing helpers ───────────────────────────────────────────────────────
    static void drawFrame      (Renderer&, int x, int y, int w, int h,
                          const std::string& title);
    static void drawPathHeader (Renderer&, int x, int y, int w,
                               const std::string& path);
    static void drawFileList   (Renderer&, int x, int y, int w, int h,
                             const std::vector<FileEntry>& entries,
                             int selection, int top, bool focused,
                             bool dirs_only);
    static void drawFileDetails(Renderer&, int x, int y,
                                const FileEntry* selected);
    static void drawInputField (Renderer&, int x, int y, int w,
                               const std::string& label,
                               const std::string& value, bool focused);
    static void drawButtons    (Renderer&, int x, int y, int w,
                            const std::string& ok_text,   bool ok_sel,
                            const std::string& can_text,  bool can_sel,
                            bool pressed);
};

#endif // FILEBROWSER_H
