#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The container format shared by the install builder and the setup stub.
//
// A finished installer is one file laid out as:
//
//   +---------------------------+  0
//   |  setup stub executable    |   an ordinary PE image - it runs as-is
//   +---------------------------+  payload_base
//   |  entry 0 bytes            |   each entry compressed (or stored) on its own
//   |  entry 1 bytes            |
//   |  ...                      |
//   +---------------------------+  toc_offset
//   |  table of contents (json) |   the whole install script + per-entry extents
//   +---------------------------+  file size - sizeof(Trailer)
//   |  Trailer                  |   fixed size, magic last-but-one field
//   +---------------------------+  EOF
//
// Appending to a PE image leaves it perfectly runnable, so the stub finds its own
// payload by opening argv[0] (really GetModuleFileName) and reading the trailer
// from the end. The TOC is json so the stub can reuse nlohmann and the builder
// can put the entire install script in there without inventing a binary schema.
namespace ar {

inline constexpr char     MAGIC[8]       = { 'G','E','D','I','S','T','U','B' };
inline constexpr uint32_t FORMAT_VERSION = 1;

// Per-entry (and TOC) compression method.
enum Method : uint32_t {
    M_STORE = 0,   // raw bytes
    M_LZMS  = 1    // Windows Compression API, COMPRESS_ALGORITHM_LZMS
};

#pragma pack(push, 1)
struct Trailer {
    uint64_t payload_base = 0;   // absolute offset of the first entry's bytes
    uint64_t toc_offset   = 0;   // absolute offset of the TOC
    uint64_t toc_csize    = 0;   // stored size of the TOC
    uint64_t toc_usize    = 0;   // size of the TOC once decompressed
    uint32_t toc_method   = M_STORE;
    uint32_t toc_crc32    = 0;   // over the stored (possibly compressed) TOC bytes
    uint32_t format       = FORMAT_VERSION;
    char     magic[8]     = { 'G','E','D','I','S','T','U','B' };
};
#pragma pack(pop)

// 4 x uint64 + 3 x uint32 + the 8 magic bytes. Pinned so a stub can never read a
// trailer a differently-built builder wrote.
static_assert(sizeof(Trailer) == 52, "Trailer layout must stay fixed across builder and stub");

uint32_t crc32(const void* data, size_t len, uint32_t seed = 0);

// True when this build can actually compress/decompress (Windows only). When it
// cannot, compress() reports M_STORE and decompress() only handles M_STORE.
bool compressionAvailable();

// Compress `in`. On any failure - or when compression does not pay for itself -
// falls back to a verbatim copy and reports M_STORE.
std::vector<uint8_t> compress(const std::vector<uint8_t>& in, Method& method_out);

// Decompress `in` (which holds `method` data) into exactly `usize` bytes.
// Returns false if the method is unsupported or the data is damaged.
bool decompress(const uint8_t* in, size_t csize, Method method, size_t usize,
                std::vector<uint8_t>& out);

// Whole-file helpers used on both sides.
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const uint8_t* data, size_t len);

} // namespace ar
