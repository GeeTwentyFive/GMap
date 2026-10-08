#include <cstdint>
#include <exception>
#include <filesystem>
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

        static std::unordered_map<std::string, nlohmann::json> map_object_registrations;
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

        auto GetMeshDataFromFile = [](std::string mesh_path) -> GOBJ* {
                static std::unordered_map<std::string, GOBJ> loaded_meshes;
                if (!loaded_meshes.contains(mesh_path)) loaded_meshes[mesh_path] = GOBJ(mesh_path);
                return &loaded_meshes[mesh_path];
        };

        struct Image {unsigned char* pixels_RGBA = nullptr; int width = 0; int height = 0;};
        auto GetImageDataFromFile = [](std::string image_path) -> Image* { int _; // (temp. throwaway for `stbi_load()`)
                static std::unordered_map<std::string, Image> loaded_images;
                if (!loaded_images.contains(image_path)) loaded_images[image_path].pixels_RGBA = stbi_load(image_path.c_str(), &loaded_images[image_path].width, &loaded_images[image_path].height, &_, 4); if (loaded_images[image_path].pixels_RGBA == NULL) throw std::runtime_error(std::string("Image '") + image_path + "' is missing/corrupt/invalid (or an allocation failure happened)");
                return &loaded_images[image_path];
        };

        GRenderer3D gr3d;

        static auto AddMesh = [&](std::string mesh_path, std::string texture_path = "") -> GRenderer3D::MeshInstance* {
                static std::unordered_map<std::string, uint64_t> gr3d_mesh_ids;
                if (!gr3d_mesh_ids.contains(mesh_path+'\0'+texture_path)) {
                        GOBJ* mesh_data = GetMeshDataFromFile(mesh_path);
                        uint32_t* texture; int texture_width; int texture_height;
                        if (texture_path != "") {
                                Image* texture_image = GetImageDataFromFile(texture_path);
                                texture = (uint32_t*)texture_image->pixels_RGBA; texture_width = texture_image->width; texture_height = texture_image->height;
                        } else {
                                static const uint32_t DEFAULT_TEXTURE[] = { 0xFFFFFFFF };
                                texture = (uint32_t*)&DEFAULT_TEXTURE; texture_width = 1; texture_height = 1;
                        }
                        gr3d_mesh_ids[mesh_path+'\0'+texture_path] = gr3d.CreateMesh(
                                (GRenderer3D::Vertex*)mesh_data->vertices.data(), mesh_data->vertices.size(),
                                (uint32_t*)mesh_data->indices.data(), mesh_data->indices.size(),
                                (uint32_t*)texture, texture_width, texture_height
                        );
                }
                return gr3d.AddMesh(gr3d_mesh_ids[mesh_path+'\0'+texture_path]);
        };

        static auto ValueInJSONArray = [](nlohmann::json value, nlohmann::json array) -> bool { return (std::find(array.begin(), array.end(), value) != array.end()); };

        struct MapObjectInstance;
        static std::vector<std::unique_ptr<MapObjectInstance>> map_object_instances;
        struct MapObjectInstance {
                nlohmann::json* base_registration;
                GRenderer3D::MeshInstance* mesh_instance;
                std::string user_model = "";
                std::string user_texture = "";
                nlohmann::json extra_data = nlohmann::json::object();

                static MapObjectInstance* Create(
                        std::string model,
                        float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
                        float rot_x = 0.0f, float rot_y = 0.0f, float rot_z = 0.0f, float rot_w = 1.0f,
                        float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
                        uint32_t color_shift_RGBA = 0xFFFFFFFF,
                        std::string user_model = "",
                        std::string user_texture = "",
                        nlohmann::json extra_data = nlohmann::json::object()
                ) { if (!map_object_registrations.contains(model)) throw std::runtime_error(std::string("No registered MapObject found with model '") + model + "'");
                        MapObjectInstance* instance = map_object_instances.emplace_back(std::make_unique<MapObjectInstance>()).get();
                        instance->base_registration = &map_object_registrations[model];
                        std::string model_path = (user_model != "") ? user_model : model;
                        std::string texture_path = (user_texture != "") ? user_texture : std::filesystem::path(model_path).replace_extension(".png").string();
                        instance->mesh_instance = AddMesh(model_path, texture_path);
                        instance->mesh_instance->position[0] = pos_x; instance->mesh_instance->position[1] = pos_y; instance->mesh_instance->position[2] = pos_z;
                        instance->mesh_instance->rotation[0] = rot_x; instance->mesh_instance->rotation[1] = rot_y; instance->mesh_instance->rotation[2] = rot_z; instance->mesh_instance->rotation[3] = rot_w;
                        instance->mesh_instance->scale[0] = scale_x; instance->mesh_instance->scale[1] = scale_y; instance->mesh_instance->scale[2] = scale_z;
                        instance->mesh_instance->color_RGBA = color_shift_RGBA;
                        instance->user_model = user_model;
                        instance->user_texture = user_texture;
                        instance->extra_data = extra_data;
                        return instance;
                };

                MapObjectInstance* Clone() {
                        return Create(
                                (*base_registration)["model"],
                                mesh_instance->position[0], mesh_instance->position[1], mesh_instance->position[2],
                                mesh_instance->rotation[0], mesh_instance->rotation[1], mesh_instance->rotation[2], mesh_instance->rotation[3],
                                mesh_instance->scale[0], mesh_instance->scale[1], mesh_instance->scale[2],
                                mesh_instance->color_RGBA,
                                user_model,
                                user_texture,
                                extra_data
                        );
                };

                void Delete() {
                        mesh_instance->Remove();
                        for (auto it = map_object_instances.begin(); it != map_object_instances.end(); it++) {
                                if (it->get() == this) { map_object_instances.erase(it); break; }
                        }
                };

                nlohmann::json SerializeToJSON() {
                        nlohmann::json map_object_instance_json = nlohmann::json::object();

                        map_object_instance_json["model"] = (*base_registration)["model"];

                        map_object_instance_json["data"] = nlohmann::json::object();
                        if (ValueInJSONArray("position", (*base_registration)["data"])) map_object_instance_json["data"]["position"] = nlohmann::json::array({mesh_instance->position[0], mesh_instance->position[1], mesh_instance->position[2]});
                        if (ValueInJSONArray("rotation", (*base_registration)["data"])) map_object_instance_json["data"]["rotation"] = nlohmann::json::array({mesh_instance->rotation[0], mesh_instance->rotation[1], mesh_instance->rotation[2], mesh_instance->rotation[3]});
                        if (ValueInJSONArray("scale", (*base_registration)["data"])) map_object_instance_json["data"]["scale"] = nlohmann::json::array({mesh_instance->scale[0], mesh_instance->scale[1], mesh_instance->scale[2]});
                        if (ValueInJSONArray("color", (*base_registration)["data"])) map_object_instance_json["data"]["color"] = mesh_instance->color_RGBA;
                        if (ValueInJSONArray("user_model", (*base_registration)["data"])) map_object_instance_json["data"]["user_model"] = user_model;
                        if (ValueInJSONArray("user_texture", (*base_registration)["data"])) map_object_instance_json["data"]["user_texture"] = user_texture;

                        if (extra_data.size() > 0) map_object_instance_json["extra_data"] = extra_data;

                        return map_object_instance_json;
                };
                static MapObjectInstance* CreateFromJSON(nlohmann::json map_object_instance_json) { if (!map_object_instance_json.is_object()) ERROR("Passed MapObjectInstance JSON is not a JSON object (it has to be curly braces)");
                        if (!map_object_instance_json.contains("model")) ERROR("Passed MapObjectInstance JSON object does not contain required key \"model\"");

                        float pos_x = 0; float pos_y = 0; float pos_z = 0;
                        if (ValueInJSONArray("position", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                pos_x = map_object_instance_json["data"]["position"][0];
                                pos_y = map_object_instance_json["data"]["position"][1];
                                pos_z = map_object_instance_json["data"]["position"][2];
                        }

                        float rot_x = 0; float rot_y = 0; float rot_z = 0; float rot_w = 1;
                        if (ValueInJSONArray("rotation", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                rot_x = map_object_instance_json["data"]["rotation"][0];
                                rot_y = map_object_instance_json["data"]["rotation"][1];
                                rot_z = map_object_instance_json["data"]["rotation"][2];
                                rot_w = map_object_instance_json["data"]["rotation"][3];
                        }

                        float scale_x = 0; float scale_y = 0; float scale_z = 0;
                        if (ValueInJSONArray("scale", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                scale_x = map_object_instance_json["data"]["scale"][0];
                                scale_y = map_object_instance_json["data"]["scale"][1];
                                scale_z = map_object_instance_json["data"]["scale"][2];
                        }

                        uint32_t color = 0xFFFFFFFF;
                        if (ValueInJSONArray("color", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                color = map_object_instance_json["data"]["color"];
                        }

                        std::string user_model = "";
                        if (ValueInJSONArray("user_model", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                user_model = map_object_instance_json["data"]["user_model"];
                        }

                        std::string user_texture = "";
                        if (ValueInJSONArray("user_texture", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                user_texture = map_object_instance_json["data"]["user_texture"];
                        }

                        nlohmann::json extra_data = nlohmann::json::object();
                        if (ValueInJSONArray("extra_data", map_object_registrations[map_object_instance_json["model"]]["data"])) {
                                extra_data = map_object_instance_json["data"]["extra_data"];
                        }

                        return Create(
                                map_object_instance_json["model"],
                                pos_x, pos_y, pos_z,
                                rot_x, rot_y, rot_z, rot_w,
                                scale_x, scale_y, scale_z,
                                color,
                                user_model,
                                user_texture,
                                extra_data
                        );
                };
        };

        // TODO
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }