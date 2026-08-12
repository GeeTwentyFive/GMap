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

#include <nlohmann/json.hpp>
#include <GWindower.hpp>
#include <glad/glad.h>
#include <GRenderer3D.hpp>
#include <GOBJ/GOBJ.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb/stb_image.h>


#define ERROR(msg) throw std::runtime_error(std::string("[ERROR] ") + __FILE__ + "@" + std::to_string(__LINE__) + " (" + __func__ + "): " + (msg))


const std::string CONFIG_FILE = "GMap.json";

const nlohmann::json DEFAULT_CONFIG = nlohmann::json::parse(R"({
        "GLOBAL_SETTINGS": {
                "disable_bounds": true,
                "disable_coloring": true,
                "disable_rotate": false,
                "disable_scale": false,
                "disable_translate": false,
                "force_grid_snap": false,
                "x_max": 340282346638528859811704183484516925440.0,
                "x_min": -340282346638528859811704183484516925440.0,
                "y_max": 340282346638528859811704183484516925440.0,
                "y_min": -340282346638528859811704183484516925440.0,
                "z_max": 340282346638528859811704183484516925440.0,
                "z_min": -340282346638528859811704183484516925440.0
        },
        "OBJECTS": []
})");

struct MapObject {
        nlohmann::json config = nlohmann::json::parse(R"({
                "model": "",
                "name": "",
                "extra_data": [],
                "override_translate": false,
                "override_rotate": false,
                "override_scale": false,
                "override_bounds": false,
                "override_coloring": false,
                "singleton": false
        })");;
        uint64_t gr3d_mesh_id;
};

struct MapObjectInstance {  // Output: name, [pos], [rot], [scale], [color], [extra_data]
        MapObject* base;  // ^ name
        GRenderer3D::MeshInstance* mesh_instance;  // ^ pos, rot, scale, color
        nlohmann::json extra_data;  // ^ extra_data
};

// for each key in `keys`: if `src` contains key: `dest[key] = src[key]`  // (for overriding defaults, if overrides are present)
void JSONAddIfHasKeys(const nlohmann::json &src, nlohmann::json &dest, const std::vector<std::string> keys) {
        for (const std::string& key : keys) { if (src.contains(key)) dest[key] = src[key]; }
}

// returns `""` on true, missing key on false
std::string JSONHasKeys(const nlohmann::json &json, const std::vector<std::string> keys) { for (const std::string& key : keys) { if (!json.contains(key)) return key; }  return ""; }

int main(int argc, char* argv[]) { try { if (!std::filesystem::is_regular_file(CONFIG_FILE)) { std::ofstream(CONFIG_FILE) << std::setw(4) << DEFAULT_CONFIG << std::endl; throw std::runtime_error(CONFIG_FILE + " not found; new one has been created with global defaults. Populate its OBJECTS array with MapObjects"); }
        nlohmann::json config; std::ifstream(CONFIG_FILE) >> config; if (!config.contains("GLOBAL_SETTINGS")) throw std::runtime_error(CONFIG_FILE + " is missing key GLOBAL_SETTINGS"); if (!config.contains("OBJECTS")) throw std::runtime_error(CONFIG_FILE + " is missing key OBJECTS");
        std::string _result; _result = JSONHasKeys(config["GLOBAL_SETTINGS"], {"disable_bounds", "disable_coloring", "disable_rotate", "disable_scale", "disable_translate", "force_grid_snap", "x_max", "x_min", "y_max", "y_min", "z_max", "z_min"}); if (_result != "") throw std::runtime_error(CONFIG_FILE + " is missing key " + _result + " in GLOBAL_SETTINGS");
        if (config["OBJECTS"].size() == 0) throw std::runtime_error(CONFIG_FILE + " has no MapObjects in OBJECTS array");

        GWindower gw{0, 0, 4, 6}; if (!gladLoadGL()) ERROR("Failed to load OpenGL");
        GRenderer3D gr3d;

        std::unordered_map<std::string, MapObject> registered_map_objects;
        for (const auto& map_object : config["OBJECTS"]) { if (!map_object.contains("model")) throw std::runtime_error(std::string("MapObject ") + std::string(map_object["name"]) + " is missing key " + R"("model")");
                MapObject registration; JSONAddIfHasKeys(map_object, registration.config, {"model", "name", "extra_data", "override_translate", "override_rotate", "override_scale", "override_bounds", "override_coloring", "singleton"}); if (!map_object.contains("name") || map_object["name"] == "") { registration.config["name"] = std::filesystem::path(map_object["model"]).stem().string(); } if (registered_map_objects.contains(registration.config["name"])) throw std::runtime_error(std::string("MapObject with name ") + std::string(registration.config["name"]) + " is already registered");
                if (!std::filesystem::is_regular_file(registration.config["model"])) throw std::runtime_error(std::string(registration.config["model"]) + " not found");
                GOBJ mesh_data(registration.config["model"]);
                std::string texture_path = std::filesystem::path(registration.config["model"]).replace_extension(".png");
                if (std::filesystem::is_regular_file(texture_path)) {
                        int texture_width, texture_height, _texture_comp;
                        unsigned char* texture = stbi_load(texture_path.c_str(), &texture_width, &texture_height, &_texture_comp, 4);
                        registration.gr3d_mesh_id = gr3d.CreateMesh(
                                (GRenderer3D::Vertex*)mesh_data.vertices.data(), mesh_data.vertices.size(),
                                (uint32_t*)mesh_data.indices.data(), mesh_data.indices.size(),
                                (uint32_t*)texture, texture_width, texture_height
                        );
                        stbi_image_free(texture);
                } else {
                        static const uint32_t DEFAULT_TEXTURE[] = { 0xFFFFFFFF };
                        registration.gr3d_mesh_id = gr3d.CreateMesh(
                                (GRenderer3D::Vertex*)mesh_data.vertices.data(), mesh_data.vertices.size(),
                                (uint32_t*)mesh_data.indices.data(), mesh_data.indices.size(),
                                DEFAULT_TEXTURE, 1, 1
                        );
                }
                registered_map_objects[registration.config["name"]] = registration;
        }

        // TODO: if (argc > 1): try to load map argv[1]
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }