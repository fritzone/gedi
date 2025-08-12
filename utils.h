#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>

// Formats file size into a human-readable string (B, KB, MB)
std::string formatSize(off_t size);

// Formats a time_t into a YYYY-MM-DD HH:MM:SS string
std::string formatTime(time_t mod_time);

// Formats a file's mode into a standard permission string (e.g., drwxr-xr--)
std::string formatPermissions(mode_t mode);

// --- UTF-8 Conversion Utility ---
std::string wchar_to_utf8(wchar_t wc);

bool ends_with(const std::string& str, const std::string& suffix);

std::vector<std::string> wrap_text(const std::string& text, int width);

#endif // UTILS_H
