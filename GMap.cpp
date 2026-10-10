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
                        if (base_registration->data.position) ImGui::DragFloat3("POSITION", mesh_instance->position);
                        if (base_registration->data.rotation) {
                                float rot_euler[3] = {
                                        atan2(2*(mesh_instance->rotation[3]*mesh_instance->rotation[0] + mesh_instance->rotation[1]*mesh_instance->rotation[2]), 1 - 2*(mesh_instance->rotation[0]*mesh_instance->rotation[0] + mesh_instance->rotation[1]*mesh_instance->rotation[1])) * (float)(180/M_PI),
                                        asin(2*(mesh_instance->rotation[3]*mesh_instance->rotation[1] - mesh_instance->rotation[2]*mesh_instance->rotation[0])) * (float)(180/M_PI),
                                        atan2(2*(mesh_instance->rotation[3]*mesh_instance->rotation[2] + mesh_instance->rotation[0]*mesh_instance->rotation[1]), 1 - 2*(mesh_instance->rotation[1]*mesh_instance->rotation[1] + mesh_instance->rotation[2]*mesh_instance->rotation[2])) * (float)(180/M_PI)
                                }; if (rot_euler[0] != rot_euler[0]) rot_euler[0] = 0.0f; if (rot_euler[1] != rot_euler[1]) rot_euler[1] = 0.0f; if (rot_euler[2] != rot_euler[2]) rot_euler[2] = 0.0f;  // (NaN fix)
                                if (ImGui::DragFloat3("ROTATION", rot_euler, 1.0f, -180.0f, 180.0f)) {
                                        float4 rot_quat = linalg::qmul(
                                                linalg::rotation_quat(float3{0, 0, 1}, rot_euler[2]*(float)(M_PI/180)),
                                                linalg::qmul(
                                                        linalg::rotation_quat(float3{0, 1, 0}, rot_euler[1]*(float)(M_PI/180)),
                                                        linalg::rotation_quat(float3{1, 0, 0}, rot_euler[0]*(float)(M_PI/180))
                                                )
                                        );
                                        mesh_instance->rotation[0] = rot_quat.x; mesh_instance->rotation[1] = rot_quat.y; mesh_instance->rotation[2] = rot_quat.z; mesh_instance->rotation[3] = rot_quat.w;
                                }
                        }
                        if (base_registration->data.scale) ImGui::DragFloat3("SCALE", mesh_instance->scale);
                        if (base_registration->data.color) {
                                ImVec4 color = ImGui::ColorConvertU32ToFloat4(mesh_instance->color_RGBA);
                                float _temp;
                                _temp = color.x; color.x = color.w; color.w = _temp;
                                _temp = color.y; color.y = color.z; color.z = _temp;
                                ImGui::ColorPicker4("COLOR", (float*)&color);
                                _temp = color.x; color.x = color.w; color.w = _temp;
                                _temp = color.y; color.y = color.z; color.z = _temp;
                                mesh_instance->color_RGBA = ImGui::ColorConvertFloat4ToU32(color);
                        }
                        if (base_registration->data.user_model) {
                                // TODO: Button -> file picker -> SetUserModelAndOrTexture()
                        }
                        if (base_registration->data.user_texture) {
                                // TODO: Button -> file picker -> SetUserModelAndOrTexture()
                        }
                        if (extra_data.size() > 0) { ImGui::NewLine();
                                uint64_t _uid = 0;
                                for (auto& [label, val] : extra_data.items()) { ImGui::PushID(_uid); switch (val.type()) { default: break;
                                        case (nlohmann::json::value_t::boolean): { bool _val = val;
                                                ImGui::Checkbox(label.c_str(), &_val); val = _val;
                                        } break;
                                        case (nlohmann::json::value_t::number_integer): { int64_t _val = val;
                                                ImGui::InputScalar(label.c_str(), ImGuiDataType_S64, &_val); val = _val;
                                        } break;
                                        case (nlohmann::json::value_t::number_unsigned): { uint64_t _val = val;
                                                ImGui::InputScalar(label.c_str(), ImGuiDataType_U64, &_val); val = _val;
                                        } break;
                                        case (nlohmann::json::value_t::number_float): { float _val = val;
                                                ImGui::InputFloat(label.c_str(), &_val, 0, 0, "%f"); val = _val;
                                        } break;
                                        case (nlohmann::json::value_t::string): { std::string _val = val;
                                                ImGui::InputText(label.c_str(), &_val); val = _val;
                                        } break;
                                } ImGui::PopID(); _uid++; }
                        }
                }

                nlohmann::json SerializeToJSON() {
                        nlohmann::json map_object_instance_json = nlohmann::json::object();

                        map_object_instance_json["name"] = base_registration->name;

                        map_object_instance_json["data"] = nlohmann::json::object();
                        if (base_registration->data.position) map_object_instance_json["data"]["position"] = nlohmann::json::array({mesh_instance->position[0], mesh_instance->position[1], mesh_instance->position[2]});
                        if (base_registration->data.rotation) map_object_instance_json["data"]["rotation"] = nlohmann::json::array({mesh_instance->rotation[0], mesh_instance->rotation[1], mesh_instance->rotation[2], mesh_instance->rotation[3]});
                        if (base_registration->data.scale) map_object_instance_json["data"]["scale"] = nlohmann::json::array({mesh_instance->scale[0], mesh_instance->scale[1], mesh_instance->scale[2]});
                        if (base_registration->data.color) map_object_instance_json["data"]["color"] = mesh_instance->color_RGBA;
                        if (base_registration->data.user_model) map_object_instance_json["data"]["user_model"] = user_model;
                        if (base_registration->data.user_texture) map_object_instance_json["data"]["user_texture"] = user_texture;

                        if (extra_data.size() > 0) map_object_instance_json["extra_data"] = extra_data;

                        return map_object_instance_json;
                }
                static MapObjectInstance* CreateFromJSON(nlohmann::json map_object_instance_json) { if (!map_object_instance_json.is_object()) ERROR("Passed MapObjectInstance JSON is not a JSON object (it has to be curly braces)");
                        const MapObjectRegistration* registration = &map_object_registrations[map_object_instance_json.at("name")];

                        float pos_x = 0; float pos_y = 0; float pos_z = 0;
                        if (registration->data.position) {
                                pos_x = map_object_instance_json.at("data").at("position").at(0);
                                pos_y = map_object_instance_json.at("data").at("position").at(1);
                                pos_z = map_object_instance_json.at("data").at("position").at(2);
                        }

                        float rot_x = 0; float rot_y = 0; float rot_z = 0; float rot_w = 1;
                        if (registration->data.rotation) {
                                rot_x = map_object_instance_json.at("data").at("rotation").at(0);
                                rot_y = map_object_instance_json.at("data").at("rotation").at(1);
                                rot_z = map_object_instance_json.at("data").at("rotation").at(2);
                                rot_w = map_object_instance_json.at("data").at("rotation").at(3);
                        }

                        float scale_x = 0; float scale_y = 0; float scale_z = 0;
                        if (registration->data.scale) {
                                scale_x = map_object_instance_json.at("data").at("scale").at(0);
                                scale_y = map_object_instance_json.at("data").at("scale").at(1);
                                scale_z = map_object_instance_json.at("data").at("scale").at(2);
                        }

                        uint32_t color = 0xFFFFFFFF;
                        if (registration->data.color) {
                                color = map_object_instance_json.at("data").at("color");
                        }

                        std::string user_model = "";
                        if (registration->data.user_model) {
                                user_model = map_object_instance_json.at("data").at("user_model");
                        }

                        std::string user_texture = "";
                        if (registration->data.user_texture) {
                                user_texture = map_object_instance_json.at("data").at("user_texture");
                        }

                        nlohmann::json extra_data = nlohmann::json::object();
                        if (registration->extra_data.size() > 0) extra_data = map_object_instance_json.at("data").at("extra_data");

                        return Create(
                                registration->name,
                                pos_x, pos_y, pos_z,
                                rot_x, rot_y, rot_z, rot_w,
                                scale_x, scale_y, scale_z,
                                color,
                                user_model,
                                user_texture,
                                extra_data
                        );
                }
        };

        if (argc > 1) { if (!std::filesystem::is_regular_file(argv[1])) throw std::runtime_error(std::string("CLI-provided file '") + argv[1] + "' not found or not regular file");
                nlohmann::json input = nlohmann::json::parse(std::ifstream(argv[1])); if (!input.is_array()) throw std::runtime_error(std::string("'") + argv[1] + "' root is not a JSON array");
                for (const nlohmann::json& map_object_instance_json : input) MapObjectInstance::CreateFromJSON(map_object_instance_json);
        }

        ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)(gw._glfwWindow), true); ImGui_ImplOpenGL3_Init("#version 460 core"); ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_NoMouseCursorChange); ImGui::GetIO().IniFilename = NULL;

        GRenderer2D gr2d;

        static const uint32_t CROSSHAIR_TEXTURE[] = {
                0x00000000, 0xFF000000, 0xFF000000, 0xFF000000, 0x00000000,
                0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFF000000,
                0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000,
                0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFF000000,
                0x00000000, 0xFF000000, 0xFF000000, 0xFF000000, 0x00000000
        };
        GRenderer2D::SpriteInstance* crosshair_sprite = gr2d.AddSprite(gr2d.CreateSprite(CROSSHAIR_TEXTURE, 5, 5)); crosshair_sprite->z_depth = -0.99f;

        MapObjectInstance* selected_map_object = nullptr;

        glClearColor(0.0, 0.0, 0.0, 1.0);
        GFramePacer gfp; gfp.target_frametime_ms = 1.0;  // (frame limiting itself not needed because using v-sync (and wait-until-input), using lib for other frame-pacing-related benefits)
        while (gw.Update(true, 0.1, true)) {
                double delta_time = gfp.Wait();

                if (gw.mouse_button_states[GWindower::MOUSEBUTTON_RIGHT]) { if (gw.locked_mouse == false) { gw.locked_mouse = true; gw.mouse_x = gw.window_width/2; gw.mouse_y = gw.window_height/2; }
                        gr3d.camera_yaw += gw.mouse_x * CAMERA_SENSITIVITY;
                        gr3d.camera_pitch += gw.mouse_y * CAMERA_SENSITIVITY;
                        static int camera_move_speed = STARTING_CAMERA_MOVE_SPEED;
                        if (gw.mouse_scroll_delta > 0.5 || gw.key_states[GWindower::KEY_UP]) camera_move_speed *= 2;
                        if (gw.mouse_scroll_delta < -0.5 || gw.key_states[GWindower::KEY_DOWN]) camera_move_speed /= 2;
                        if (camera_move_speed < 1) camera_move_speed = 1;
                        gr3d.camera_pos[1] += (-gw.key_states[GWindower::KEY_LEFT_CONTROL] + gw.key_states[GWindower::KEY_SPACE]) * camera_move_speed * delta_time;
                        float2 move_dir = float2{
                                (float)(-gw.key_states[GWindower::KEY_A] + gw.key_states[GWindower::KEY_D]),
                                (float)(-gw.key_states[GWindower::KEY_W] + gw.key_states[GWindower::KEY_S])
                        };
                        if (linalg::length2(move_dir) != 0) move_dir = linalg::normalize(move_dir);
                        move_dir = linalg::rot((float)(gr3d.camera_yaw*(2*M_PI/65536)), move_dir);
                        gr3d.camera_pos[0] += -move_dir.x * camera_move_speed * delta_time;
                        gr3d.camera_pos[2] += -move_dir.y * camera_move_speed * delta_time;

                        if (gw.key_states[GWindower::KEY_ZERO]) { gr3d.camera_pos[0] = 0; gr3d.camera_pos[1] = 0; gr3d.camera_pos[2] = 0; }

                        static bool _obj_pick_ray_casted = false;
                        if (gw.mouse_button_states[GWindower::MOUSEBUTTON_LEFT] && !_obj_pick_ray_casted) { _obj_pick_ray_casted = true;
                                float3 ray_dir{
                                        cosf((gr3d.camera_yaw*(2*M_PI/65536))-M_PI/2) * cosf(gr3d.camera_pitch*(2*M_PI/65536)),
                                        sinf(gr3d.camera_pitch*(2*M_PI/65536)),
                                        sinf((gr3d.camera_yaw*(2*M_PI/65536))-M_PI/2) * cosf(gr3d.camera_pitch*(2*M_PI/65536))
                                };
                                float closest_hit_map_object_distance = FLT_MAX;
                                for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) {
                                        gcollision2::AABB map_object_instance_aabb = map_object_instance->AABB();
                                        if (map_object_instance_aabb.ContainsPoint(float3{gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]})) continue;  // (skip if camera is inside of obj (to make it so you can select objs inside of other objs))
                                        gcollision2::RayHitExtraInfo extra_hit_info;
                                        if (gcollision2::IntersectRayAABB(
                                                float3{gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]},
                                                ray_dir,
                                                map_object_instance_aabb,
                                                &extra_hit_info
                                        )) if (extra_hit_info.distance < closest_hit_map_object_distance) { closest_hit_map_object_distance = extra_hit_info.distance;
                                                selected_map_object = map_object_instance.get();
                                        }
                                }
                                if (closest_hit_map_object_distance == FLT_MAX) selected_map_object = nullptr;  // (none hit; deselect)
                        } else if (!gw.mouse_button_states[GWindower::MOUSEBUTTON_LEFT]) _obj_pick_ray_casted = false;
                } else gw.locked_mouse = false;

                static bool _cloned = false;
                if (gw.key_states[GWindower::KEY_LEFT_ALT] && gw.key_states[GWindower::KEY_D] && selected_map_object != nullptr && !_cloned) { _cloned = true;
                        selected_map_object = selected_map_object->Clone();
                } else if (!gw.key_states[GWindower::KEY_D]) _cloned = false;

                if (gw.key_states[GWindower::KEY_DELETE] && selected_map_object != nullptr) {
                        selected_map_object->Delete();
                        selected_map_object = nullptr;
                }

                static bool show_ui = true;

                static bool _show_ui_toggled = false;
                if (gw.key_states[GWindower::KEY_LEFT_ALT] && gw.key_states[GWindower::KEY_H] && !_show_ui_toggled) { _show_ui_toggled = true; show_ui = !show_ui; }
                else if (!gw.key_states[GWindower::KEY_H]) _show_ui_toggled = false;

                if (show_ui) { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame(); {
                        ImGui::SetNextWindowPos({(float)gw.window_width, 0.0f}, ImGuiCond_Always, {1.0f, 0.0f});
                        ImGui::Begin("ADD", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                for (const auto& [registered_model, _] : map_object_registrations) {
                                        if (ImGui::Button(std::filesystem::path(registered_model).stem().c_str())) selected_map_object = MapObjectInstance::Create(registered_model, gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]);
                                }
                        } ImGui::End();

                        if (selected_map_object != nullptr) {
                                ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always, {0.0f, 0.0f});
                                ImGui::Begin("INSPECTOR", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                        selected_map_object->DrawInspectorContents();
                                } ImGui::End();
                        }
                } ImGui::Render(); }

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glViewport(0, 0, gw.window_width, gw.window_height);  // (leaving this inside of the mainloop in case I want to support windowed operation in the future)
                gr3d.DrawFrame(gw.window_width, gw.window_height);
                if (show_ui) {
                        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                        glClear(GL_DEPTH_BUFFER_BIT);
                        crosshair_sprite->size[0] = gw.window_height/180.0; crosshair_sprite->size[1] = gw.window_height/180.0;
                        crosshair_sprite->position[0] = (gw.window_width/2.0 - crosshair_sprite->size[0]/2.0); crosshair_sprite->position[1] = (gw.window_height/2.0 - crosshair_sprite->size[1]/2.0);
                        gr2d.DrawFrame(gw.window_width, gw.window_height);
                }


                if(gw.key_states[GWindower::KEY_ESCAPE]) {
                        nlohmann::json output = nlohmann::json::array();
                        for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) {
                                output.push_back(map_object_instance->SerializeToJSON());
                        }
                        std::cout << std::setw(4) << output << std::endl;

                        break;
                }
        }
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }