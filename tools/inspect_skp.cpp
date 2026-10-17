#include "skp_archive.hpp"
#include "skp_model.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: skp_inspect <SKP path>\n";
        return 2;
    }
    try {
        std::ifstream f(argv[1], std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot open input");
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
        auto h = skp::read_legacy_header(bytes);
        std::cout << "Archive version: " << h.version
                  << "\nVersion map entries: " << h.classes.size()
                  << "\nPayload offset: " << h.payload_offset << "\n";
        for (const auto &c : h.classes)
            std::cout << c.name << " " << c.version << "\n";
        auto model = skp::load_model_file(argv[1]);
        std::cout << "Independent model read succeeded\nFaces: " << model->faces.size()
                  << "\nEdges: " << model->edges.size() << "\nVertices: " << model->vertices.size()
                  << "\nLoops: " << model->loops.size() << "\nGroups: " << model->groups.size()
                  << "\nInstances: " << model->instances.size()
                  << "\nMaterials: " << model->metadata.materials.size()
                  << "\nLayers: " << model->metadata.layers.size() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
