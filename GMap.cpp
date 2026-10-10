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


bool IsValueInJSONArray(nlohmann::json value, nlohmann::json array) { return (std::find(array.begin(), array.end(), value) != array.end()); };

int main(int argc, char* argv[]) { try {
        nlohmann::json stdin_json_array = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>())); if (stdin_json_array.type() != nlohmann::json::value_t::array) throw std::runtime_error("stdin is not a JSON array (read 'README.md')"); if (stdin_json_array.size() == 0) throw std::runtime_error("stdin JSON array is empty");
        struct MapObjectRegistration {
                std::string name;
                std::string model;
                struct {
                        bool position = false;
                        bool rotation = false;
                        bool scale = false;
                        bool color = false;
                        bool user_model = false;
                        bool user_texture = false;
                } data;
                nlohmann::json extra_data = nlohmann::json::object();;
        };
        static std::unordered_map<std::string, MapObjectRegistration> map_object_registrations;
        for (const auto& map_object : stdin_json_array) { if (!map_object.contains("name")) throw std::runtime_error("MapObject in stdin JSON array is missing key \"name\""); if (!map_object.contains("model")) throw std::runtime_error("MapObject in stdin JSON array is missing key \"model\"");
                if (map_object_registrations.contains(map_object["name"])) throw std::runtime_error(std::string("MapObject with name '") + std::string(map_object["name"]) + "' is already registered");
                MapObjectRegistration* registration = &map_object_registrations.emplace(map_object["name"], MapObjectRegistration{}).first->second;
                registration->name = map_object["name"];
                registration->model = std::filesystem::path(map_object["model"]).lexically_normal().generic_string();
                if (map_object.contains("data")) {
                        if (IsValueInJSONArray("position", map_object["data"])) registration->data.position = true;
                        if (IsValueInJSONArray("rotation", map_object["data"])) registration->data.rotation = true;
                        if (IsValueInJSONArray("scale", map_object["data"])) registration->data.scale = true;
                        if (IsValueInJSONArray("color", map_object["data"])) registration->data.color = true;
                        if (IsValueInJSONArray("user_model", map_object["data"])) registration->data.user_model = true;
                        if (IsValueInJSONArray("user_texture", map_object["data"])) registration->data.user_texture = true;
                }
                if (map_object.contains("extra_data")) registration->extra_data = map_object["extra_data"];
        }

        GWindower gw{0, 0, 4, 6}; gw.locked_mouse = false;
        if (!gladLoadGL()) ERROR("Failed to load OpenGL functions");

        GRenderer3D gr3d;

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

        static auto AddMeshToScene = [&](std::string mesh_path, std::string texture_path = "") -> GRenderer3D::MeshInstance* {
                static std::unordered_map<std::string, uint64_t> gr3d_mesh_ids;
                if (!gr3d_mesh_ids.contains(mesh_path+'\0'+texture_path)) {
                        GOBJ* mesh_data = GetMeshDataFromFile(mesh_path);
                        uint32_t* texture; int texture_width; int texture_height;
                        if (texture_path != "" && std::filesystem::is_regular_file(texture_path)) {
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

        static auto GetMeshAABBFromFile = [&](std::string mesh_path) -> gcollision2::AABB* {
                static std::unordered_map<std::string, gcollision2::AABB> calculated_aabbs;
                if (!calculated_aabbs.contains(mesh_path)) {
                        gcollision2::AABB aabb{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
                        for (const GOBJ::Vertex& vertex : GetMeshDataFromFile(mesh_path)->vertices) {
                                if (vertex.pos[0] < aabb.min.x) aabb.min.x = vertex.pos[0];
                                else if (vertex.pos[0] > aabb.max.x) aabb.max.x = vertex.pos[0];
                                if (vertex.pos[1] < aabb.min.y) aabb.min.y = vertex.pos[1];
                                else if (vertex.pos[1] > aabb.max.y) aabb.max.y = vertex.pos[1];
                                if (vertex.pos[2] < aabb.min.z) aabb.min.z = vertex.pos[2];
                                else if (vertex.pos[2] > aabb.max.z) aabb.max.z = vertex.pos[2];
                        }
                        calculated_aabbs[mesh_path] = aabb;
                }
                return &calculated_aabbs[mesh_path];
        };

        struct MapObjectInstance;
        static std::vector<std::unique_ptr<MapObjectInstance>> map_object_instances;
        struct MapObjectInstance {
                MapObjectRegistration* base_registration;  // name & model
                GRenderer3D::MeshInstance* mesh_instance;  // position, rotation, scale, & color
                nlohmann::json extra_data = nlohmann::json::object();
                private:
                std::string user_model = "";
                std::string user_texture = "";

                public:

                static MapObjectInstance* Create(
                        std::string name,
                        float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
                        float rot_x = 0.0f, float rot_y = 0.0f, float rot_z = 0.0f, float rot_w = 1.0f,
                        float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
                        uint32_t color_shift_RGBA = 0xFFFFFFFF,
                        std::string user_model = "",
                        std::string user_texture = "",
                        nlohmann::json extra_data = nlohmann::json::object()
                ) { if (!map_object_registrations.contains(name)) ERROR("No registered MapObject found with name '" + name + "'");
                        MapObjectInstance* instance = map_object_instances.emplace_back(std::unique_ptr<MapObjectInstance>(new MapObjectInstance())).get();
                        instance->base_registration = &map_object_registrations[name];
                        std::string model_path = (user_model != "") ? user_model : instance->base_registration->model;
                        std::string texture_path = (user_texture != "") ? user_texture : std::string(model_path).replace(model_path.rfind('.'), std::string::npos, ".png");
                        instance->mesh_instance = AddMeshToScene(model_path, texture_path);
                        instance->mesh_instance->position[0] = pos_x; instance->mesh_instance->position[1] = pos_y; instance->mesh_instance->position[2] = pos_z;
                        instance->mesh_instance->rotation[0] = rot_x; instance->mesh_instance->rotation[1] = rot_y; instance->mesh_instance->rotation[2] = rot_z; instance->mesh_instance->rotation[3] = rot_w;
                        instance->mesh_instance->scale[0] = scale_x; instance->mesh_instance->scale[1] = scale_y; instance->mesh_instance->scale[2] = scale_z;
                        instance->mesh_instance->color_RGBA = color_shift_RGBA;
                        instance->user_model = user_model;
                        instance->user_texture = user_texture;
                        instance->extra_data = extra_data;
                        return instance;
                } private: MapObjectInstance(){}; public:

                void SetUserModelAndOrTexture(std::string model_path = "", std::string texture_path = "") { if (model_path == "" && texture_path == "") return;
                        if (model_path == "") {
                                if (user_model != "") model_path = user_model;
                                else model_path = base_registration->model;
                        } else user_model = model_path;
                        if (texture_path == "") {
                                if (user_texture != "") texture_path = user_texture;
                                else texture_path = std::string(model_path).replace(model_path.rfind('.'), std::string::npos, ".png");
                        } else user_texture = texture_path;
                        GRenderer3D::MeshInstance* previous_mesh_instance = mesh_instance;
                        mesh_instance = AddMeshToScene(model_path, texture_path);
                        mesh_instance->position[0] = previous_mesh_instance->position[0]; mesh_instance->position[1] = previous_mesh_instance->position[1]; mesh_instance->position[2] = previous_mesh_instance->position[2];
                        mesh_instance->rotation[0] = previous_mesh_instance->rotation[0]; mesh_instance->rotation[1] = previous_mesh_instance->rotation[1]; mesh_instance->rotation[2] = previous_mesh_instance->rotation[2]; mesh_instance->rotation[3] = previous_mesh_instance->rotation[3];
                        mesh_instance->scale[0] = previous_mesh_instance->scale[0]; mesh_instance->scale[1] = previous_mesh_instance->scale[1]; mesh_instance->scale[2] = previous_mesh_instance->scale[2];
                        mesh_instance->color_RGBA = previous_mesh_instance->color_RGBA;
                        previous_mesh_instance->Remove();
                }
                std::string GetUserModel() { return user_model; }
                std::string GetUserTexture() { return user_texture; }

                float4x4 Transform() {
                        return linalg::mul(
                                linalg::translation_matrix(float3{mesh_instance->position[0], mesh_instance->position[1], mesh_instance->position[2]}),
                                linalg::mul(
                                        linalg::rotation_matrix(float4{mesh_instance->rotation[0], mesh_instance->rotation[1], mesh_instance->rotation[2], mesh_instance->rotation[3]}),
                                        linalg::scaling_matrix(float3{mesh_instance->scale[0], mesh_instance->scale[1], mesh_instance->scale[2]})
                                )
                        );
                }

                gcollision2::AABB AABB() {
                        gcollision2::AABB aabb{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
                        gcollision2::AABB* mesh_aabb = (user_model != "") ? GetMeshAABBFromFile(user_model) : GetMeshAABBFromFile(base_registration->model);
                        float3 mesh_aabb_cuboid[8]{
                                {mesh_aabb->min.x, mesh_aabb->min.y, mesh_aabb->min.z},
                                {mesh_aabb->min.x, mesh_aabb->min.y, mesh_aabb->max.z},
                                {mesh_aabb->min.x, mesh_aabb->max.y, mesh_aabb->min.z},
                                {mesh_aabb->min.x, mesh_aabb->max.y, mesh_aabb->max.z},
                                {mesh_aabb->max.x, mesh_aabb->min.y, mesh_aabb->min.z},
                                {mesh_aabb->max.x, mesh_aabb->min.y, mesh_aabb->max.z},
                                {mesh_aabb->max.x, mesh_aabb->max.y, mesh_aabb->min.z},
                                {mesh_aabb->max.x, mesh_aabb->max.y, mesh_aabb->max.z}
                        };
                        float4x4 transform = Transform();
                        for (float3& vertex : mesh_aabb_cuboid) vertex = linalg::mul(transform, float4{vertex, 1.0f}).xyz();
                        for (const float3& vertex : mesh_aabb_cuboid) {
                                if (vertex.x < aabb.min.x) aabb.min.x = vertex.x;
                                else if (vertex.x > aabb.max.x) aabb.max.x = vertex.x;
                                if (vertex.y < aabb.min.y) aabb.min.y = vertex.y;
                                else if (vertex.y > aabb.max.y) aabb.max.y = vertex.y;
                                if (vertex.z < aabb.min.z) aabb.min.z = vertex.z;
                                else if (vertex.z > aabb.max.z) aabb.max.z = vertex.z;
                        }
                        return aabb;
                }

                MapObjectInstance* Clone() {
                        return Create(
                                base_registration->name,
                                mesh_instance->position[0], mesh_instance->position[1], mesh_instance->position[2],
                                mesh_instance->rotation[0], mesh_instance->rotation[1], mesh_instance->rotation[2], mesh_instance->rotation[3],
                                mesh_instance->scale[0], mesh_instance->scale[1], mesh_instance->scale[2],
                                mesh_instance->color_RGBA,
                                user_model,
                                user_texture,
                                extra_data
                        );
                }

                void Delete() {  // Deletes faster by copying last MapObject instance (pointer) in map_object_instances to this instance's place and then deleting that one from the end of the vector (rather than using std::vector::erase() which reorders the elements)
                        mesh_instance->Remove();
                        for (size_t i = 0; i < map_object_instances.size(); i++) {
                                if (map_object_instances[i].get() == this) {
                                        map_object_instances[i] = std::move(map_object_instances.back());
                                        break;
                                }
                        }
                        map_object_instances.pop_back();
                }

                void DrawInspectorContents() {
                        // TODO
                }

                nlohmann::json SerializeToJSON() {
                        // TODO
                }
                static MapObjectInstance* CreateFromJSON(nlohmann::json map_object_instance_json) {
                        // TODO
                }
        };

        // TODO

        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }