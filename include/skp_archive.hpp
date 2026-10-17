#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace skp {
struct ClassVersion {
    std::string name;
    std::uint32_t version;
};
struct ArchiveHeader {
    std::string version;
    std::vector<ClassVersion> classes;
    std::size_t payload_offset;
};
// Parses the observed legacy archive prefix, not the model geometry.
// Unsupported encodings/versions and truncated input raise std::runtime_error.
ArchiveHeader read_legacy_header(const std::vector<std::uint8_t> &bytes);
} // namespace skp
