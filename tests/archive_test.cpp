#include "skp_archive.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>
static void require(bool b) {
    if (!b)
        throw std::runtime_error("Assertion failed");
}
int main(int argc, char **argv) {
    try {
        require(argc == 2);
        std::ifstream f(argv[1], std::ios::binary);
        require(bool(f));
        std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
        auto h = skp::read_legacy_header(b);
        require(h.version == "{15.0.1}" && h.classes.size() == 58 && h.payload_offset == 1975);
        bool found = false;
        for (auto c : h.classes)
            if (c.name == "CSketchUpModel") {
                require(c.version == 24);
                found = true;
            }
        require(found);
        // Every prefix truncation must be rejected, including inside strings and integers.
        for (std::size_t n = 0; n < h.payload_offset; ++n) {
            bool rejected = false;
            try {
                skp::read_legacy_header(std::vector<std::uint8_t>(b.begin(), b.begin() + n));
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            require(rejected);
        }
        b[4] = 'X';
        bool rejected = false;
        try {
            skp::read_legacy_header(b);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected);
        std::cout << "Header and truncation tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
