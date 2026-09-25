#include "Install.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace inst {

// ---------------------------------------------------------------------------
//  Platform helpers
// ---------------------------------------------------------------------------
std::wstring toWide(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring w((size_t)std::max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), w.data(), n);
    return w;
#else
    return std::wstring(utf8.begin(), utf8.end());
#endif
}

std::string toUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::string expandEnv(const std::string& s) {
#ifdef _WIN32
    const std::wstring in = toWide(s);
    DWORD n = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (n == 0) return s;
    std::wstring out(n, L'\0');
    n = ExpandEnvironmentStringsW(in.c_str(), out.data(), n);
    if (n == 0) return s;
    out.resize(n > 0 ? n - 1 : 0);
    return toUtf8(out);
#else
    return s;
#endif
}

std::string nativePath(const std::string& p) {
    std::string s = p;
#ifdef _WIN32
    std::replace(s.begin(), s.end(), '/', '\\');
    while (s.size() > 3 && (s.back() == '\\')) s.pop_back();
#endif
    return s;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return nativePath(b);
    if (b.empty()) return nativePath(a);
    std::string s = a;
    if (s.back() != '\\' && s.back() != '/') s += '\\';
    s += b;
    return nativePath(s);
}

std::string shellFolder(const std::string& which) {
#ifdef _WIN32
    int csidl = -1;
    if      (which == "start_menu") csidl = CSIDL_PROGRAMS;
    else if (which == "desktop")    csidl = CSIDL_DESKTOPDIRECTORY;
    else if (which == "temp") {
        wchar_t buf[MAX_PATH + 1] = {};
        const DWORD n = GetTempPathW(MAX_PATH, buf);
        return n ? nativePath(toUtf8(std::wstring(buf, n))) : std::string();
    }
    if (csidl < 0) return {};
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path)))
        return nativePath(toUtf8(path));
#else
    (void)which;
#endif
    return {};
}

bool ensureDir(const std::string& path) {
    std::error_code ec;
    if (path.empty()) return false;
    fs::create_directories(fs::path(path), ec);
    return fs::is_directory(fs::path(path), ec);
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

bool isDirWritable(const std::string& path) {
    // Walk up to the first component that exists, and probe that one: the target
    // directory itself usually does not exist yet at the point we need to know.
    std::error_code ec;
    fs::path p = fs::path(path);
    while (!p.empty() && !fs::exists(p, ec)) {
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    if (p.empty() || !fs::is_directory(p, ec)) return false;

    const fs::path probe = p / ".gedi-setup-probe.tmp";
    std::ofstream f(probe, std::ios::binary);
    if (!f) return false;
    f.close();
    fs::remove(probe, ec);
    return true;
}

namespace {
bool caseInsensitiveEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
//  Bootstrap
// ---------------------------------------------------------------------------
bool Bootstrap::prepare(const payload::Payload& pl, const std::vector<std::string>& names,
                        std::string& err) {
    const std::string tmp = shellFolder("temp");
    if (tmp.empty()) { err = "cannot locate the temporary directory"; return false; }

#ifdef _WIN32
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "gedi-setup-%lu", (unsigned long)GetCurrentProcessId());
#else
    const char* stamp = "gedi-setup";
#endif
    dir = joinPath(tmp, stamp);
    if (!ensureDir(dir)) { err = "cannot create " + dir; return false; }

    for (const std::string& want : names) {
        const payload::Entry* found = nullptr;
        for (const auto& e : pl.entries()) {
            // Match on the file name so the script can name "SDL2.dll" without
            // caring which component or subdirectory it is installed from.
            const size_t slash = e.path.find_last_of('/');
            const std::string base = (slash == std::string::npos) ? e.path : e.path.substr(slash + 1);
            if (caseInsensitiveEqual(base, want)) { found = &e; break; }
        }
        if (!found) { err = "the payload has no bootstrap file named " + want; return false; }

        std::vector<uint8_t> bytes;
        if (!pl.extract(*found, bytes, err)) return false;
        if (!ar::writeFile(joinPath(dir, want), bytes.data(), bytes.size())) {
            err = "cannot write " + joinPath(dir, want);
            return false;
        }
    }

#ifdef _WIN32
    // Resolve the delay-loaded SDL2.dll from here, and let the font search - which
    // tries a plain relative name first - find VGA9.F16 in the same place.
    SetDllDirectoryW(toWide(dir).c_str());
    SetCurrentDirectoryW(toWide(dir).c_str());
#endif
    return true;
}

void Bootstrap::cleanup() {
    if (dir.empty()) return;
#ifdef _WIN32
    // Step out of the directory before removing it, or the delete fails.
    const std::string tmp = shellFolder("temp");
    if (!tmp.empty()) SetCurrentDirectoryW(toWide(tmp).c_str());
    SetDllDirectoryW(nullptr);
#endif
    std::error_code ec;
    fs::remove_all(fs::path(dir), ec);
    dir.clear();
}

// ---------------------------------------------------------------------------
//  Shortcuts and registry
// ---------------------------------------------------------------------------
namespace {

#ifdef _WIN32
bool createShortcut(const std::string& lnk_path, const std::string& target,
                    const std::string& workdir, const std::string& description,
                    const std::string& icon) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**)&link);
    if (FAILED(hr) || !link) return false;

    link->SetPath(toWide(target).c_str());
    if (!workdir.empty())     link->SetWorkingDirectory(toWide(workdir).c_str());
    if (!description.empty()) link->SetDescription(toWide(description).c_str());
    if (!icon.empty())        link->SetIconLocation(toWide(icon).c_str(), 0);

    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, (void**)&file);
    bool ok = false;
    if (SUCCEEDED(hr) && file) {
        ok = SUCCEEDED(file->Save(toWide(lnk_path).c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

bool regSetString(HKEY key, const wchar_t* name, const std::string& value) {
    const std::wstring w = toWide(value);
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)w.c_str(),
                          (DWORD)((w.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool regSetDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof value) == ERROR_SUCCESS;
}

std::wstring uninstallKeyPath(const std::string& key_name) {
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + toWide(key_name);
}
#endif

} // namespace

// ---------------------------------------------------------------------------
//  Install
// ---------------------------------------------------------------------------
uint64_t sizeOf(const payload::Payload& pl, const std::vector<std::string>& ids) {
    uint64_t total = 0;
    for (const auto& e : pl.entries())
        if (std::find(ids.begin(), ids.end(), e.component) != ids.end()) total += e.usize;
    return total;
}

Result install(const payload::Payload& pl, const Plan& plan, const Progress& progress) {
    Result r;
    const json& spec = pl.spec();

    if (!ensureDir(plan.dir)) { r.error = "cannot create " + plan.dir; return r; }

    const uint64_t total = sizeOf(pl, plan.components);
    uint64_t done = 0;

    json manifest;
    manifest["product"]   = spec.value("product", json::object());
    manifest["dir"]       = plan.dir;
    manifest["files"]     = json::array();
    manifest["dirs"]      = json::array();
    manifest["shortcuts"] = json::array();

    // ---- files -------------------------------------------------------------
    for (const auto& e : pl.entries()) {
        if (std::find(plan.components.begin(), plan.components.end(), e.component) == plan.components.end())
            continue;

        const std::string dest = joinPath(plan.dir, nativePath(e.path));
        const std::string parent = fs::path(dest).parent_path().string();
        if (!parent.empty() && !ensureDir(parent)) { r.error = "cannot create " + parent; return r; }

        if (progress) progress(e.path, done, total);

        std::vector<uint8_t> bytes;
        if (!pl.extract(e, bytes, r.error)) return r;
        if (!ar::writeFile(dest, bytes.data(), bytes.size())) {
            r.error = "cannot write " + dest;
            return r;
        }

        manifest["files"].push_back(e.path);
        if (!parent.empty() && parent != plan.dir) {
            const std::string rel = fs::path(e.path).parent_path().generic_string();
            if (!rel.empty()) {
                auto& dirs = manifest["dirs"];
                if (std::find(dirs.begin(), dirs.end(), json(rel)) == dirs.end()) dirs.push_back(rel);
            }
        }

        done += e.usize;
        r.bytes_written += e.usize;
        ++r.files_written;
    }
    if (progress) progress("", total, total);

    // ---- the uninstaller: our own image, minus the payload ------------------
    const std::string unins = joinPath(plan.dir, UNINSTALLER_NAME);
    {
        std::vector<uint8_t> self;
        if (ar::readFile(payload::Payload::modulePath(), self) && self.size() >= pl.stubSize()) {
            if (!ar::writeFile(unins, self.data(), (size_t)pl.stubSize()))
                r.error = "cannot write " + unins;      // not fatal; reported at the end
        }
    }

    // ---- shortcuts ---------------------------------------------------------
#ifdef _WIN32
    if (spec.contains("shortcuts")) {
        for (const auto& sc : spec["shortcuts"]) {
            const std::string where = sc.value("location", std::string("start_menu"));
            if (where == "start_menu" && !plan.start_menu) continue;
            if (where == "desktop"    && !plan.desktop)    continue;

            std::string folder = shellFolder(where);
            if (folder.empty()) continue;

            // A Start-menu group keeps the product's shortcuts together.
            const std::string group = sc.value("group", std::string());
            if (where == "start_menu" && !group.empty()) {
                folder = joinPath(folder, group);
                if (!ensureDir(folder)) continue;
                manifest["dirs"].push_back(json("\x01start_menu_group:" + group));
            }

            const std::string name   = sc.value("name", std::string("Application"));
            const std::string target = joinPath(plan.dir, nativePath(sc.value("target", std::string())));
            const std::string icon   = sc.contains("icon")
                                     ? joinPath(plan.dir, nativePath(sc["icon"].get<std::string>()))
                                     : target;
            const std::string lnk    = joinPath(folder, name + ".lnk");

            if (createShortcut(lnk, target, plan.dir, sc.value("description", std::string()), icon))
                manifest["shortcuts"].push_back(lnk);
        }
    }

    // ---- Add/Remove Programs ----------------------------------------------
    const json product = spec.value("product", json::object());
    const std::string key_name = product.value("install_key", product.value("name", std::string("app")));
    if (plan.register_uninstall) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, uninstallKeyPath(key_name).c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            regSetString(key, L"DisplayName",     product.value("name", std::string()));
            regSetString(key, L"DisplayVersion",  product.value("version", std::string()));
            regSetString(key, L"Publisher",       product.value("publisher", std::string()));
            regSetString(key, L"InstallLocation", plan.dir);
            regSetString(key, L"UninstallString", "\"" + unins + "\"");
            regSetString(key, L"DisplayIcon",     unins);
            if (product.contains("website")) regSetString(key, L"URLInfoAbout", product["website"].get<std::string>());
            regSetDword(key, L"NoModify", 1);
            regSetDword(key, L"NoRepair", 1);
            regSetDword(key, L"EstimatedSize", (DWORD)(r.bytes_written / 1024));
            RegCloseKey(key);
            manifest["registry_key"] = key_name;
        }
    }
#endif

    // ---- manifest ----------------------------------------------------------
    {
        const std::string text = manifest.dump(2);
        ar::writeFile(joinPath(plan.dir, MANIFEST_NAME),
                      reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    r.ok = r.error.empty();
    return r;
}

// ---------------------------------------------------------------------------
//  Uninstall
// ---------------------------------------------------------------------------
bool loadManifest(const std::string& dir, json& out) {
    std::vector<uint8_t> bytes;
    if (!ar::readFile(joinPath(dir, MANIFEST_NAME), bytes)) return false;
    out = json::parse(std::string(bytes.begin(), bytes.end()), nullptr, false);
    return !out.is_discarded();
}

Result uninstall(const json& manifest, const Progress& progress) {
    Result r;
    const std::string dir = manifest.value("dir", std::string());
    if (dir.empty()) { r.error = "the manifest does not say where the product was installed"; return r; }

    std::error_code ec;

    const auto files = manifest.value("files", json::array());
    const uint64_t total = files.size() ? files.size() : 1;
    uint64_t done = 0;

    for (const auto& f : files) {
        const std::string rel = f.get<std::string>();
        const std::string path = joinPath(dir, nativePath(rel));
        if (progress) progress(rel, done, total);
        fs::remove(fs::path(path), ec);
        ++done;
        ++r.files_written;
    }

#ifdef _WIN32
    for (const auto& s : manifest.value("shortcuts", json::array()))
        fs::remove(fs::path(s.get<std::string>()), ec);

    if (manifest.contains("registry_key"))
        RegDeleteKeyW(HKEY_CURRENT_USER, uninstallKeyPath(manifest["registry_key"].get<std::string>()).c_str());
#endif

    // Sub-directories, deepest first, and only when they came out empty.
    std::vector<std::string> dirs;
    for (const auto& d : manifest.value("dirs", json::array())) {
        const std::string s = d.get<std::string>();
        if (s.rfind("\x01start_menu_group:", 0) == 0) {
#ifdef _WIN32
            const std::string group = s.substr(std::string("\x01start_menu_group:").size());
            const std::string folder = joinPath(shellFolder("start_menu"), group);
            fs::remove(fs::path(folder), ec);
#endif
            continue;
        }
        dirs.push_back(s);
    }
    std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) {
        return a.size() > b.size();
    });
    for (const auto& d : dirs) fs::remove(fs::path(joinPath(dir, nativePath(d))), ec);

    // The manifest itself goes last; the uninstaller executable cannot delete
    // itself while it is running, so it is scheduled for the next reboot and the
    // directory is left for that to finish off.
    fs::remove(fs::path(joinPath(dir, MANIFEST_NAME)), ec);
#ifdef _WIN32
    const std::string self = payload::Payload::modulePath();
    MoveFileExW(toWide(self).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
#endif
    fs::remove(fs::path(dir), ec);   // succeeds only when nothing is left

    if (progress) progress("", total, total);
    r.ok = true;
    return r;
}

} // namespace inst
