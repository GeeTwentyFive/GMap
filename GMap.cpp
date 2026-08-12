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
                "force_grid_snap": false,
                "grid_size": 1.0,
                "x_max": 340282346638528859811704183484516925440.0,
                "x_min": -340282346638528859811704183484516925440.0,
                "y_max": 340282346638528859811704183484516925440.0,
                "y_min": -340282346638528859811704183484516925440.0,
                "z_max": 340282346638528859811704183484516925440.0,
                "z_min": -340282346638528859811704183484516925440.0
        },
        "OBJECTS": []
})");

// for each key in `keys`: if `src` contains key: `dest[key] = src[key]`  // (for overriding defaults, if overrides are present)
void JSONAddIfHasKeys(const nlohmann::json &src, nlohmann::json &dest, const std::vector<std::string> keys) {
        for (const std::string& key : keys) { if (src.contains(key)) dest[key] = src[key]; }
}

// returns `""` on true, missing key on false
std::string JSONHasKeys(const nlohmann::json &json, const std::vector<std::string> keys) { for (const std::string& key : keys) { if (!json.contains(key)) return key; }  return ""; }

int main(int argc, char* argv[]) { try { if (!std::filesystem::is_regular_file(CONFIG_FILE)) { std::ofstream(CONFIG_FILE) << std::setw(4) << DEFAULT_CONFIG << std::endl; throw std::runtime_error(CONFIG_FILE + " not found; new one has been created with global defaults. Populate its OBJECTS array with MapObjects"); }
        nlohmann::json config; std::ifstream(CONFIG_FILE) >> config; if (!config.contains("GLOBAL_SETTINGS")) throw std::runtime_error(CONFIG_FILE + " is missing key GLOBAL_SETTINGS"); if (!config.contains("OBJECTS")) throw std::runtime_error(CONFIG_FILE + " is missing key OBJECTS");
        std::string _result; _result = JSONHasKeys(config["GLOBAL_SETTINGS"], {"force_grid_snap", "grid_size", "x_max", "x_min", "y_max", "y_min", "z_max", "z_min"}); if (_result != "") throw std::runtime_error(CONFIG_FILE + " is missing key " + _result + " in GLOBAL_SETTINGS");
        if (config["OBJECTS"].size() == 0) throw std::runtime_error(CONFIG_FILE + " has no MapObjects in OBJECTS array");

        GWindower gw{0, 0, 4, 6}; if (!gladLoadGL()) ERROR("Failed to load OpenGL");
        GRenderer3D gr3d;

        struct MapObject {
                nlohmann::json config = nlohmann::json::parse(R"({
                        "model": "",
                        "name": "",
                        "data": [],
                        "extra_data": {},
                        "singleton": false
                })");
                uint64_t gr3d_mesh_id;
        };
        std::unordered_map<std::string, MapObject> registered_map_objects;
        for (const auto& map_object : config["OBJECTS"]) { if (!map_object.contains("model")) throw std::runtime_error(std::string("MapObject ") + std::string(map_object["name"]) + " is missing key " + R"("model")");
                MapObject registration; JSONAddIfHasKeys(map_object, registration.config, {"model", "name", "data", "extra_data", "singleton"}); if (!map_object.contains("name") || map_object["name"] == "") { registration.config["name"] = std::filesystem::path(map_object["model"]).stem().string(); } if (registered_map_objects.contains(registration.config["name"])) throw std::runtime_error(std::string("MapObject with name ") + std::string(registration.config["name"]) + " is already registered");
                if (!std::filesystem::is_regular_file(registration.config["model"])) throw std::runtime_error(std::string(registration.config["model"]) + " not found");
                GOBJ mesh_data(registration.config["model"]);
                std::string texture_path = std::filesystem::path(registration.config["model"]).replace_extension(".png");
                if (std::filesystem::is_regular_file(texture_path)) {
                        int texture_width, texture_height, _texture_comp;
                        unsigned char* texture = stbi_load(texture_path.c_str(), &texture_width, &texture_height, &_texture_comp, 4); if (texture == NULL) throw std::runtime_error(std::string("The texture ") + texture_path + " for model " + std::string(registration.config["model"]) + " for MapObject " + std::string(registration.config["name"]) + " is corrupt/invalid (or an allocation failure happened)");
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

        // TEMP; TEST:
        for (auto [k, v] : registered_map_objects) {
                std::cout << "Name: " << k << std::endl;
                std::cout << "Config: " << std::setw(4) << v.config << std::endl;
                std::cout << "Registered Mesh ID: " << v.gr3d_mesh_id << std::endl;
                std::cout << std::endl;
        }

        // struct MapObjectInstance {  // Output: name, [pos], [rot], [scale], [color], [extra_data]
        //         uint64_t uid;
        //         MapObject* base;  // ^ name
        //         GRenderer3D::MeshInstance* mesh_instance;  // ^ pos, rot, scale, color
        //         nlohmann::json extra_data;  // ^ extra_data
        // };
        // std::vector<MapObjectInstance> map_object_instances;
        // auto InstantiateMapObject = [&](
        //         std::string base,
        //         float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
        //         float rot_x_degrees = 0.0f, float rot_y_degrees = 0.0f, float rot_z_degrees = 0.0f,
        //         float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
        //         uint32_t color_shift_RGBA = 0xFFFFFFFF
        // ) -> MapObjectInstance* { if (!registered_map_objects.contains(base)) ERROR(base + " not found in registered map objects");
        //         static uint64_t _last_uid = 0;
        //         MapObjectInstance* instance = &map_object_instances.emplace_back();
        //         instance->uid = _last_uid; _last_uid++;
        //         instance->base = &registered_map_objects[base];
        //         instance->extra_data = instance->base->config["extra_data"];
        //         instance->mesh_instance = gr3d.AddMesh(instance->base->gr3d_mesh_id);
        //         return instance;
        // };
        // auto RemoveMapObject = [&](uint64_t uid) {
        //         for (auto it = map_object_instances.begin(); it != map_object_instances.end(); it++) {
        //                 if (it->uid == uid) {
        //                         it->mesh_instance->Remove();
        //                         map_object_instances.erase(it); return;
        //                 }
        //         }
        //         ERROR(std::string("Failed to find & erase MapObjectInstance with uid ") + std::to_string(uid));
        // };

        // TODO: if (argc > 1): try to load map argv[1]
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }