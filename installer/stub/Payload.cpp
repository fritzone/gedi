#include "Payload.h"

#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace payload {

using nlohmann::json;

std::string Payload::modulePath() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);          // path longer than MAX_PATH
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, buf.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(need > 0 ? need - 1 : 0, '\0');
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, buf.c_str(), -1, out.data(), need, nullptr, nullptr);
    return out;
#else
    return {};
#endif
}

bool Payload::open(std::string& err) {
    path_ = modulePath();
    if (path_.empty()) { err = "cannot determine the path of the running executable"; return false; }

    std::ifstream f(path_, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open " + path_; return false; }

    const std::streamoff size = f.tellg();
    if (size < (std::streamoff)sizeof(ar::Trailer)) { err = "no payload attached"; return false; }

    f.seekg(size - (std::streamoff)sizeof(ar::Trailer));
    ar::Trailer tr{};
    f.read(reinterpret_cast<char*>(&tr), sizeof tr);
    if (!f) { err = "cannot read the payload trailer"; return false; }

    if (std::memcmp(tr.magic, ar::MAGIC, sizeof tr.magic) != 0) { err = "no payload attached"; return false; }
    if (tr.format != ar::FORMAT_VERSION)                        { err = "unsupported payload format"; return false; }
    if (tr.toc_offset + tr.toc_csize > (uint64_t)size)          { err = "payload is truncated"; return false; }

    std::vector<uint8_t> toc_packed((size_t)tr.toc_csize);
    f.seekg((std::streamoff)tr.toc_offset);
    if (tr.toc_csize) f.read(reinterpret_cast<char*>(toc_packed.data()), (std::streamsize)tr.toc_csize);
    if (!f) { err = "cannot read the table of contents"; return false; }

    if (ar::crc32(toc_packed.data(), toc_packed.size()) != tr.toc_crc32) {
        err = "the table of contents is damaged - the installer may be a partial download";
        return false;
    }

    std::vector<uint8_t> toc_raw;
    if (!ar::decompress(toc_packed.data(), toc_packed.size(), (ar::Method)tr.toc_method,
                        (size_t)tr.toc_usize, toc_raw)) {
        err = "cannot decompress the table of contents";
        return false;
    }

    spec_ = json::parse(std::string(toc_raw.begin(), toc_raw.end()), nullptr, false);
    if (spec_.is_discarded()) { err = "the table of contents is not valid json"; return false; }

    entries_.clear();
    if (spec_.contains("entries")) {
        for (const auto& e : spec_["entries"]) {
            Entry it;
            it.path      = e.value("path", std::string());
            it.component = e.value("component", std::string());
            it.offset    = e.value("offset", (uint64_t)0);
            it.csize     = e.value("csize",  (uint64_t)0);
            it.usize     = e.value("usize",  (uint64_t)0);
            it.method    = e.value("method", (uint32_t)0);
            it.crc32     = e.value("crc32",  (uint32_t)0);
            if (!it.path.empty()) entries_.push_back(std::move(it));
        }
    }
    if (entries_.empty()) { err = "the payload contains no files"; return false; }

    trailer_ = tr;
    open_    = true;
    return true;
}

bool Payload::extract(const Entry& e, std::vector<uint8_t>& out, std::string& err) const {
    std::ifstream f(path_, std::ios::binary);
    if (!f) { err = "cannot reopen " + path_; return false; }

    std::vector<uint8_t> packed((size_t)e.csize);
    f.seekg((std::streamoff)(trailer_.payload_base + e.offset));
    if (e.csize) f.read(reinterpret_cast<char*>(packed.data()), (std::streamsize)e.csize);
    if (!f) { err = "cannot read " + e.path + " from the payload"; return false; }

    if (!ar::decompress(packed.data(), packed.size(), (ar::Method)e.method, (size_t)e.usize, out)) {
        err = "cannot decompress " + e.path;
        return false;
    }
    if (ar::crc32(out.data(), out.size()) != e.crc32) {
        err = e.path + " failed its checksum";
        return false;
    }
    return true;
}

} // namespace payload
