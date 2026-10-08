#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>
#include <vector>
#include <stdint.h>
#include <unordered_map>
#include <math.h>
#ifndef M_PI  // (Windows fix)
#define M_PI 3.14159265358979323846
#endif
#include <memory>
#include <algorithm>
#include <tuple>

#include <nlohmann/json.hpp>
#include <libGCollision2/libGCollision2.hpp>
#include <GWindower.hpp>
#include <glad/glad.h>
#include <GRenderer3D.hpp>
#include <GOBJ/GOBJ.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb/stb_image.h>
#include <GFramePacer/GFramePacer.hpp>
#include <linalg/linalg.h>
using namespace linalg::aliases;
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <GRenderer2D.hpp>


#define ERROR(msg) throw std::runtime_error(std::string("[ERROR] ") + __FILE__ + "@" + std::to_string(__LINE__) + " (" + __func__ + "): " + (msg))


const int CAMERA_SENSITIVITY = 16;
const int STARTING_CAMERA_MOVE_SPEED = 8;

int main(int argc, char* argv[]) { try {
        nlohmann::json config = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>())); if (config.type() != nlohmann::json::value_t::array) throw std::runtime_error("stdin is empty, or stdin is not valid JSON, or stdin root is not a JSON array (read 'README.md')"); if (config.size() == 0) throw std::runtime_error("stdin JSON array is empty");

        std::unordered_map<std::string, nlohmann::json> map_object_registrations;
        for (const auto& map_object : config) { if (!map_object.contains("model")) throw std::runtime_error("MapObject in stdin config is missing key \"model\"");
                nlohmann::json registration = nlohmann::json::parse(R"({
                        "model": "",
                        "data": [],
                        "extra_data": {}
                })");
                for (const auto& [key, _] : registration.items()) if (map_object.contains(key)) registration[key] = map_object[key];
                if (map_object_registrations.contains(registration["model"])) throw std::runtime_error(std::string("MapObject with model '") + std::string(registration["model"]) + "' is already registered");
                map_object_registrations[registration["model"]] = registration;
        }

        GWindower gw{0, 0, 4, 6}; gw.locked_mouse = false;
        if (!gladLoadGL()) ERROR("Failed to load OpenGL functions");

        auto GetMesh = [](std::string mesh_path) -> GOBJ* {
                static std::unordered_map<std::string, GOBJ> loaded_meshes;
                if (!loaded_meshes.contains(mesh_path)) loaded_meshes[mesh_path] = GOBJ(mesh_path);
                return &loaded_meshes[mesh_path];
        };

        struct Image {unsigned char* pixels_RGBA = nullptr; int width = 0; int height = 0;};
        auto GetImage = [](std::string image_path) -> Image* { int _; // (temp. throwaway for `stbi_load()`)
                static std::unordered_map<std::string, Image> loaded_images;
                if (!loaded_images.contains(image_path)) loaded_images[image_path].pixels_RGBA = stbi_load(image_path.c_str(), &loaded_images[image_path].width, &loaded_images[image_path].height, &_, 4); if (loaded_images[image_path].pixels_RGBA == NULL) throw std::runtime_error(std::string("Image '") + image_path + "' is corrupt/invalid (or an allocation failure happened)");
                return &loaded_images[image_path];
        };

        GRenderer3D gr3d;

        auto AddMesh = [&](std::string mesh_path, std::string texture_path = "") -> GRenderer3D::MeshInstance* {
                static std::unordered_map<std::string, uint64_t> gr3d_mesh_ids;
                if (!gr3d_mesh_ids.contains()) {
                        // TODO
                }
                return &gr3d_mesh_ids[]; // TODO
        };

        // TODO

        struct MapObjectInstance {
                // TODO

                MapObjectInstance() {
                        // TODO
                }

                std::string Serialize() {
                        // TODO
                };
                static MapObjectInstance Deserialize(std::string json) {
                        // TODO
                };
        };
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }