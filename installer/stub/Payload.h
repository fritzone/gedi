#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "../common/Archive.h"

// Reads the payload that the builder welded onto the back of this executable.
//
// The stub is also its own uninstaller: the first `stubSize()` bytes of the file
// are the bare program, so the installer writes exactly that prefix out as
// unins.exe - an uninstaller with none of the payload's weight.
namespace payload {

struct Entry {
    std::string path;        // install-dir relative, forward slashes
    std::string component;
    uint64_t    offset = 0;  // relative to the payload base
    uint64_t    csize  = 0;
    uint64_t    usize  = 0;
    uint32_t    method = 0;
    uint32_t    crc32  = 0;
};

class Payload {
public:
    // Locate and validate this executable's payload. Returns false with a reason
    // when the file carries none (a bare stub, e.g. the uninstaller).
    bool open(std::string& err);

    bool isOpen() const { return open_; }

    // The install script, exactly as the builder read it, plus "entries".
    const nlohmann::json& spec() const { return spec_; }
    const std::vector<Entry>& entries() const { return entries_; }

    // Decompress one entry, verifying its CRC.
    bool extract(const Entry& e, std::vector<uint8_t>& out, std::string& err) const;

    // Size of the executable image without the payload.
    uint64_t stubSize() const { return trailer_.payload_base; }

    // Full path of the running executable.
    static std::string modulePath();

private:
    bool                 open_ = false;
    std::string          path_;
    ar::Trailer          trailer_;
    // Always an object, so the wizard can query it with value() even when this
    // build is the payload-less uninstaller.
    nlohmann::json       spec_ = nlohmann::json::object();
    std::vector<Entry>   entries_;
};

} // namespace payload
