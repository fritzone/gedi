#include "utils.h"

#include <dirent.h>
#include <pwd.h>      // For getpwuid() to get user names
#include <grp.h>      // For getgrgid() to get group names
#include <unistd.h>
#include <sys/stat.h>

#include <chrono>
#include <cctype>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

std::string formatPermissions(mode_t mode) {
    std::string perms = "----------";
    if (S_ISDIR(mode)) perms[0] = 'd';
    if (S_ISLNK(mode)) perms[0] = 'l';

    if (mode & S_IRUSR) perms[1] = 'r';
    if (mode & S_IWUSR) perms[2] = 'w';
    if (mode & S_IXUSR) perms[3] = 'x';

    if (mode & S_IRGRP) perms[4] = 'r';
    if (mode & S_IWGRP) perms[5] = 'w';
    if (mode & S_IXGRP) perms[6] = 'x';

    if (mode & S_IROTH) perms[7] = 'r';
    if (mode & S_IWOTH) perms[8] = 'w';
    if (mode & S_IXOTH) perms[9] = 'x';

    return perms;
}

std::string formatTime(time_t mod_time) {
    char buf[80];
    struct tm* timeinfo = localtime(&mod_time);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    return buf;
}

std::string formatSize(off_t size) {
    if (size < 1024) return std::to_string(size) + " B";
    if (size < 1024 * 1024) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << (float)size / 1024.0f;
        return ss.str() + " KB";
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << (float)size / (1024.0f * 1024.0f);
    return ss.str() + " MB";
}

bool ends_with(const std::string &str, const std::string &suffix) {
    if (str.length() < suffix.length()) { return false; }
    return str.rfind(suffix) == (str.length() - suffix.length());
}

std::string wchar_to_utf8(wchar_t wc) {
    std::string str;
    if (wc < 0x80) { str.push_back(static_cast<char>(wc)); }
    else if (wc < 0x800) { str.push_back(static_cast<char>(0xC0 | (wc >> 6))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); }
    else if (wc < 0x10000) { str.push_back(static_cast<char>(0xE0 | (wc >> 12))); str.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); }
    else if (wc < 0x110000) { str.push_back(static_cast<char>(0xF0 | (wc >> 18))); str.push_back(static_cast<char>(0x80 | ((wc >> 12) & 0x3F))); str.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); }
    return str;
}

std::vector<std::string> wrap_text(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) {
        lines.push_back(text);
        return lines;
    }

    std::string current_line;
    std::string current_word;
    std::stringstream ss(text);

    while(ss >> current_word) {
        if (current_line.length() + current_word.length() + 1 > (size_t)width) {
            lines.push_back(current_line);
            current_line = current_word;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
            }
            current_line += current_word;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    if (lines.empty() && !text.empty()) {
        lines.push_back(text);
    }

    return lines;
}

std::string get_full_path(const std::string &filename_part) {
    // Trim leading and trailing spaces
    std::string filename = filename_part;
    filename.erase(0, filename.find_first_not_of(" \t\n\r\f\v"));
    filename.erase(filename.find_last_not_of(" \t\n\r\f\v") + 1);

    fs::path p(filename);

    // If the path is not absolute, make it absolute relative to the current working directory
    if (!p.is_absolute()) {
        p = fs::absolute(p);
    }

    // Resolve all . and .. elements
    try {
        p = fs::canonical(p);
    } catch (const fs::filesystem_error& e) {
        // Handle error (e.g., file does not exist)
        return filename_part;
    }

    return p.string();
}

std::string get_filename_from_path(const std::string &full_path)
{
    fs::path p(full_path);
    return p.filename().string();
}

// Turn a font file stem into a human-readable name: "FM-T-437" -> "Fm T 437".
static std::string pretty_font_name(std::string stem) {
    for (char& c : stem) if (c == '-' || c == '_') c = ' ';
    std::string out;
    bool word_start = true;
    for (char c : stem) {
        if (c == ' ') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            word_start = true;
            continue;
        }
        if (word_start) { out += (char)std::toupper((unsigned char)c); word_start = false; }
        else            { out += (char)std::tolower((unsigned char)c); }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::pair<std::string, std::string>> listEditorFonts() {
    std::vector<std::pair<std::string, std::string>> fonts;
    fonts.push_back({"Default", ""});   // the built-in modified VGA font

    // Locate the fonts directory.
    std::vector<std::string> dirs;
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        dirs.push_back((exe.parent_path() / "fonts").string());
        dirs.push_back((exe.parent_path().parent_path() / "share/gedi/fonts").string());
    }
    dirs.push_back("fonts");
    dirs.push_back("/usr/share/gedi/fonts");

    std::string dir;
    for (const auto& d : dirs) if (fs::is_directory(d, ec)) { dir = d; break; }
    if (dir.empty()) return fonts;

    std::vector<std::pair<std::string, std::string>> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        fs::path p = entry.path();
        std::string ext = p.extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".f16") continue;
        std::string stem = p.stem().string();
        std::string up = stem;
        for (char& c : up) c = (char)std::toupper((unsigned char)c);
        if (up == "VGA9") continue;   // that's already represented by "Default"
        found.push_back({pretty_font_name(stem), p.string()});
    }
    std::sort(found.begin(), found.end());
    for (auto& f : found) fonts.push_back(std::move(f));
    return fonts;
}
