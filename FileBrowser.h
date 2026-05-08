#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <string>
#include <vector>
#include <sys/stat.h>
#include "Renderer.h"

struct FileEntry {
    std::string name;
    bool is_directory;
    off_t size;
    time_t mod_time;
    mode_t permissions;
    std::string owner;
    std::string group;
};

class FileBrowser {
public:
    static std::string open(Renderer& renderer);
    static std::string save(Renderer& renderer, const std::string& current_filename);
};

#endif
