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
