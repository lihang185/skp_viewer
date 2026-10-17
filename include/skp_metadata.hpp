#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace skp {
struct Texture {
    std::string filename;
    double width = 1, height = 1;
    size_t pixel_width = 0, pixel_height = 0;
    int format = 0;
    // Original embedded image bytes (PNG/JPEG/etc.), independent of archive lifetime.
    std::vector<unsigned char> image_data;
};
struct Material {
    std::string name, description;
    unsigned char rgba[4] = {0, 0, 0, 0};
    // The serialized value is transparency; the C API exposes its complement.
    double transparency = 0;
    bool use_opacity = false;
    int type = 0;
    bool colorize_shift = true;
    std::unique_ptr<Texture> texture;
};
struct Layer {
    std::string name;
    bool hidden = false;
    Material material;
};
struct Camera {
    double eye[3] = {0, 0, 1}, target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    bool perspective = true;
    double fov = 35, orthographic_height = 1, aspect_ratio = 0;
};
struct Metadata {
    std::string description;
    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::unique_ptr<Layer>> layers;
    Camera camera;
};
} // namespace skp
