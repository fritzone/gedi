#include "Archive.h"

#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <compressapi.h>
#endif

namespace ar {

// ---------------------------------------------------------------------------
//  CRC-32 (IEEE, the zip/png polynomial), table built on first use.
// ---------------------------------------------------------------------------
uint32_t crc32(const void* data, size_t len, uint32_t seed) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t c = seed ^ 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
//  Compression
//
//  Windows ships an LZMS compressor in Cabinet.dll (Windows 8 and later), which
//  is a far better deal than vendoring a compression library: no third-party
//  code, no build plumbing, and ratios in the neighbourhood of LZMA. A builder
//  running anywhere else simply stores the bytes - the format carries a per-entry
//  method, so a stored payload installs exactly the same way, just larger.
// ---------------------------------------------------------------------------
bool compressionAvailable() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::vector<uint8_t> compress(const std::vector<uint8_t>& in, Method& method_out) {
    method_out = M_STORE;
    if (in.empty()) return {};

#ifdef _WIN32
    COMPRESSOR_HANDLE h = nullptr;
    if (CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) {
        SIZE_T needed = 0;
        // First call sizes the output; ERROR_INSUFFICIENT_BUFFER is the expected
        // way it answers, not a failure.
        Compress(h, (PVOID)in.data(), in.size(), nullptr, 0, &needed);
        if (needed == 0) needed = in.size() + in.size() / 2 + 1024;

        std::vector<uint8_t> out(needed);
        SIZE_T written = 0;
        if (Compress(h, (PVOID)in.data(), in.size(), out.data(), out.size(), &written)) {
            CloseCompressor(h);
            // Only keep the compressed form when it actually saves something;
            // already-compressed payloads (PNG, zlib1.dll) often grow.
            if (written > 0 && written < in.size()) {
                out.resize(written);
                method_out = M_LZMS;
                return out;
            }
            return in;
        }
        CloseCompressor(h);
    }
#endif
    return in;
}

bool decompress(const uint8_t* in, size_t csize, Method method, size_t usize,
                std::vector<uint8_t>& out) {
    if (method == M_STORE) {
        if (csize != usize) return false;
        out.assign(in, in + csize);
        return true;
    }

#ifdef _WIN32
    if (method == M_LZMS) {
        DECOMPRESSOR_HANDLE h = nullptr;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) return false;
        out.resize(usize);
        SIZE_T written = 0;
        const BOOL ok = Decompress(h, (PVOID)in, csize, out.data(), out.size(), &written);
        CloseDecompressor(h);
        if (!ok || written != usize) return false;
        return true;
    }
#else
    (void)in; (void)csize; (void)usize; (void)out;
#endif
    return false;
}

// ---------------------------------------------------------------------------
//  File helpers
// ---------------------------------------------------------------------------
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize((size_t)n);
    f.seekg(0);
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return (bool)f;
}

bool writeFile(const std::string& path, const uint8_t* data, size_t len) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (len) f.write(reinterpret_cast<const char*>(data), (std::streamsize)len);
    return (bool)f;
}

} // namespace ar
