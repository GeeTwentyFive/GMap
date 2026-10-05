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

const std::string CONFIG_FILE = "GMap.json";

int main(int argc, char* argv[]) { try { if (!std::filesystem::is_regular_file(CONFIG_FILE)) throw std::runtime_error(CONFIG_FILE + " not found in current working directory");
        nlohmann::json config; std::ifstream(CONFIG_FILE) >> config; if (config.type() != nlohmann::json::value_t::array) throw std::runtime_error(CONFIG_FILE + " root is not an array");
        if (config.size() == 0) throw std::runtime_error(CONFIG_FILE + " has no MapObjects");

        GWindower gw{0, 0, 4, 6}; gw.locked_mouse = false; if (!gladLoadGL()) ERROR("Failed to load OpenGL functions");
        GRenderer3D gr3d;

        struct MapObject {
                nlohmann::json config = nlohmann::json::parse(R"({
                        "model": "",
                        "name": "",
                        "data": [],
                        "extra_data": {}
                })");
                uint64_t gr3d_mesh_id;
                gcollision2::AABB aabb{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
        };

        std::unordered_map<std::string, MapObject> registered_map_objects;
        for (const auto& map_object : config) { if (!map_object.contains("model")) throw std::runtime_error(std::string("MapObject ") + std::string(map_object["name"]) + " is missing key " + R"("model")");
                MapObject registration;
                for (const auto& [key, _] : registration.config.items()) if (map_object.contains(key)) registration.config[key] = map_object[key];
                if (!std::filesystem::is_regular_file(registration.config["model"])) throw std::runtime_error(std::string(registration.config["model"]) + " not found");
                if (!map_object.contains("name") || map_object["name"] == "") { registration.config["name"] = std::filesystem::path(map_object["model"]).stem().string(); } if (registered_map_objects.contains(registration.config["name"])) throw std::runtime_error(std::string("MapObject with name ") + std::string(registration.config["name"]) + " is already registered");
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
                for (const GOBJ::Vertex& vertex : mesh_data.vertices) {
                        if (vertex.pos[0] < registration.aabb.min.x) registration.aabb.min.x = vertex.pos[0];
                        else if (vertex.pos[0] > registration.aabb.max.x) registration.aabb.max.x = vertex.pos[0];
                        if (vertex.pos[1] < registration.aabb.min.y) registration.aabb.min.y = vertex.pos[1];
                        else if (vertex.pos[1] > registration.aabb.max.y) registration.aabb.max.y = vertex.pos[1];
                        if (vertex.pos[2] < registration.aabb.min.z) registration.aabb.min.z = vertex.pos[2];
                        else if (vertex.pos[2] > registration.aabb.max.z) registration.aabb.max.z = vertex.pos[2];
                }
                registered_map_objects[registration.config["name"]] = registration;
        }

        struct MapObjectInstance {  // Output: name, [pos], [rot], [scale], [color], [extra_data]
                uint64_t uid;
                MapObject* base;  // ^ name
                GRenderer3D::MeshInstance* mesh_instance;  // ^ pos, rot, scale, color
                nlohmann::json extra_data;  // ^ extra_data
        };
        std::vector<std::unique_ptr<MapObjectInstance>> map_object_instances;
        auto InstantiateMapObject = [&](
                std::string base,
                float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
                float quat_x = 0.0f, float quat_y = 0.0f, float quat_z = 0.0f, float quat_w = 1.0f,
                float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
                uint32_t color_shift_RGBA = 0xFFFFFFFF
        ) -> MapObjectInstance* { if (!registered_map_objects.contains(base)) ERROR(base + " not found in registered map objects");
                static uint64_t _last_uid = 0;
                MapObjectInstance* instance = map_object_instances.emplace_back(std::make_unique<MapObjectInstance>()).get();
                instance->uid = _last_uid; _last_uid++;
                instance->base = &registered_map_objects[base];
                instance->extra_data = instance->base->config["extra_data"];
                instance->mesh_instance = gr3d.AddMesh(instance->base->gr3d_mesh_id);
                instance->mesh_instance->position[0] = pos_x; instance->mesh_instance->position[1] = pos_y; instance->mesh_instance->position[2] = pos_z;
                instance->mesh_instance->rotation[0] = quat_x; instance->mesh_instance->rotation[1] = quat_y; instance->mesh_instance->rotation[2] = quat_z; instance->mesh_instance->rotation[3] = quat_w;
                instance->mesh_instance->scale[0] = scale_x; instance->mesh_instance->scale[1] = scale_y; instance->mesh_instance->scale[2] = scale_z;
                instance->mesh_instance->color_RGBA = color_shift_RGBA;
                return instance;
        };

        if (argc > 1) { if (!std::filesystem::is_regular_file(argv[1])) throw std::runtime_error(std::string(argv[1]) + " not found or not regular file");
                nlohmann::json input = nlohmann::json::parse(std::ifstream(argv[1])); if (!input.is_array()) throw std::runtime_error(std::string(argv[1]) + " root is not an array");
                for (const nlohmann::json& map_object_instance_json : input) { if (!map_object_instance_json.is_object()) throw std::runtime_error(std::string(argv[1]) + " contains non-object type");
                        if (!map_object_instance_json.contains("name")) throw std::runtime_error(std::string("Object in ") + argv[1] + " is missing key 'name'");
                        if (!registered_map_objects.contains(map_object_instance_json["name"])) throw std::runtime_error(std::string("Registered MapObject not found for object with name ") + std::string(map_object_instance_json["name"]) + " in " + argv[1]);

                        float pos_x = 0; float pos_y = 0; float pos_z = 0;
                        if (std::find(registered_map_objects[map_object_instance_json["name"]].config["data"].begin(), registered_map_objects[map_object_instance_json["name"]].config["data"].end(),
                                "position") != registered_map_objects[map_object_instance_json["name"]].config["data"].end()
                        ) {
                                pos_x = map_object_instance_json["position"][0];
                                pos_y = map_object_instance_json["position"][1];
                                pos_z = map_object_instance_json["position"][2];
                        }
                        float rot_x = 0; float rot_y = 0; float rot_z = 0; float rot_w = 1;
                        if (std::find(registered_map_objects[map_object_instance_json["name"]].config["data"].begin(), registered_map_objects[map_object_instance_json["name"]].config["data"].end(),
                                "rotation") != registered_map_objects[map_object_instance_json["name"]].config["data"].end()
                        ) {
                                rot_x = map_object_instance_json["rotation"][0];
                                rot_y = map_object_instance_json["rotation"][1];
                                rot_z = map_object_instance_json["rotation"][2];
                                rot_w = map_object_instance_json["rotation"][3];
                        }
                        float scale_x = 1.0; float scale_y = 1.0; float scale_z = 1.0;
                        if (std::find(registered_map_objects[map_object_instance_json["name"]].config["data"].begin(), registered_map_objects[map_object_instance_json["name"]].config["data"].end(),
                                "scale") != registered_map_objects[map_object_instance_json["name"]].config["data"].end()
                        ) {
                                scale_x = map_object_instance_json["scale"][0];
                                scale_y = map_object_instance_json["scale"][1];
                                scale_z = map_object_instance_json["scale"][2];
                        }
                        uint32_t color = 0xFFFFFFFF;
                        if (std::find(registered_map_objects[map_object_instance_json["name"]].config["data"].begin(), registered_map_objects[map_object_instance_json["name"]].config["data"].end(),
                                "color") != registered_map_objects[map_object_instance_json["name"]].config["data"].end()
                        ) { color = map_object_instance_json["color"]; }

                        MapObjectInstance* map_object_instance = InstantiateMapObject(
                                map_object_instance_json["name"],
                                pos_x, pos_y, pos_z,
                                rot_x, rot_y, rot_z, rot_w,
                                scale_x, scale_y, scale_z,
                                color
                        );
                        if (registered_map_objects[map_object_instance_json["name"]].config["extra_data"].size() > 0) map_object_instance->extra_data = map_object_instance_json["extra_data"];
                }
        }

        ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)(gw._glfwWindow), true); ImGui_ImplOpenGL3_Init("#version 460 core"); ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange); ImGui::GetIO().IniFilename = NULL;
        static const std::string IMGUI_CACHE_PATH = (std::filesystem::temp_directory_path() / std::filesystem::path(argv[0]).stem()).string()+"_imgui.ini";
        if (std::filesystem::is_regular_file(IMGUI_CACHE_PATH)) ImGui::LoadIniSettingsFromDisk(IMGUI_CACHE_PATH.c_str());

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
                                        float4x4 map_object_instance_transform = linalg::mul(
                                                linalg::translation_matrix(float3{map_object_instance->mesh_instance->position[0], map_object_instance->mesh_instance->position[1], map_object_instance->mesh_instance->position[2]}),
                                                linalg::mul(
                                                        linalg::rotation_matrix(float4{map_object_instance->mesh_instance->rotation[0], map_object_instance->mesh_instance->rotation[1], map_object_instance->mesh_instance->rotation[2], map_object_instance->mesh_instance->rotation[3]}),
                                                        linalg::scaling_matrix(float3{map_object_instance->mesh_instance->scale[0], map_object_instance->mesh_instance->scale[1], map_object_instance->mesh_instance->scale[2]})
                                                )
                                        );
                                        float3 map_object_instance_aabb_cuboid[8]{
                                                {map_object_instance->base->aabb.min.x, map_object_instance->base->aabb.min.y, map_object_instance->base->aabb.min.z},
                                                {map_object_instance->base->aabb.min.x, map_object_instance->base->aabb.min.y, map_object_instance->base->aabb.max.z},
                                                {map_object_instance->base->aabb.min.x, map_object_instance->base->aabb.max.y, map_object_instance->base->aabb.min.z},
                                                {map_object_instance->base->aabb.min.x, map_object_instance->base->aabb.max.y, map_object_instance->base->aabb.max.z},
                                                {map_object_instance->base->aabb.max.x, map_object_instance->base->aabb.min.y, map_object_instance->base->aabb.min.z},
                                                {map_object_instance->base->aabb.max.x, map_object_instance->base->aabb.min.y, map_object_instance->base->aabb.max.z},
                                                {map_object_instance->base->aabb.max.x, map_object_instance->base->aabb.max.y, map_object_instance->base->aabb.min.z},
                                                {map_object_instance->base->aabb.max.x, map_object_instance->base->aabb.max.y, map_object_instance->base->aabb.max.z}
                                        };
                                        for (float3& vertex : map_object_instance_aabb_cuboid) vertex = linalg::mul(map_object_instance_transform, float4{vertex, 1.0f}).xyz();
                                        gcollision2::AABB map_object_instance_aabb{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
                                        for (const float3& vertex : map_object_instance_aabb_cuboid) {
                                                if (vertex.x < map_object_instance_aabb.min.x) map_object_instance_aabb.min.x = vertex.x;
                                                else if (vertex.x > map_object_instance_aabb.max.x) map_object_instance_aabb.max.x = vertex.x;
                                                if (vertex.y < map_object_instance_aabb.min.y) map_object_instance_aabb.min.y = vertex.y;
                                                else if (vertex.y > map_object_instance_aabb.max.y) map_object_instance_aabb.max.y = vertex.y;
                                                if (vertex.z < map_object_instance_aabb.min.z) map_object_instance_aabb.min.z = vertex.z;
                                                else if (vertex.z > map_object_instance_aabb.max.z) map_object_instance_aabb.max.z = vertex.z;
                                        }
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
                        MapObjectInstance* clone = InstantiateMapObject(
                                selected_map_object->base->config["name"],
                                selected_map_object->mesh_instance->position[0], selected_map_object->mesh_instance->position[1], selected_map_object->mesh_instance->position[2],
                                selected_map_object->mesh_instance->rotation[0], selected_map_object->mesh_instance->rotation[1], selected_map_object->mesh_instance->rotation[2], selected_map_object->mesh_instance->rotation[3],
                                selected_map_object->mesh_instance->scale[0], selected_map_object->mesh_instance->scale[1], selected_map_object->mesh_instance->scale[2],
                                selected_map_object->mesh_instance->color_RGBA
                        );
                        clone->extra_data = selected_map_object->extra_data;
                        selected_map_object = clone;
                } else if (!gw.key_states[GWindower::KEY_D]) _cloned = false;

                if (gw.key_states[GWindower::KEY_DELETE] && selected_map_object != nullptr) {
                        for (auto it = map_object_instances.begin(); it != map_object_instances.end(); it++) {
                                if (it->get()->uid == selected_map_object->uid) {
                                        it->get()->mesh_instance->Remove();
                                        map_object_instances.erase(it); break;
                                }
                        }
                        selected_map_object = nullptr;
                }

                static bool show_ui = true;

                static bool _show_ui_toggled = false;
                if (gw.key_states[GWindower::KEY_LEFT_ALT] && gw.key_states[GWindower::KEY_H] && !_show_ui_toggled) { _show_ui_toggled = true; show_ui = !show_ui; }
                else if (!gw.key_states[GWindower::KEY_H]) _show_ui_toggled = false;

                if (show_ui) { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame(); ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode); {
                        ImGui::Begin("ADD", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)); {
                                for (const auto& [map_object, _] : registered_map_objects) {
                                        if (ImGui::Button(map_object.c_str())) { selected_map_object = InstantiateMapObject(map_object, gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]); }
                                }
                        } ImGui::End();

                        if (selected_map_object != nullptr) {
                                ImGui::Begin("INSPECTOR", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)); {
                                        if (std::find(selected_map_object->base->config["data"].begin(), selected_map_object->base->config["data"].end(),
                                                "position") != selected_map_object->base->config["data"].end()
                                        ) ImGui::DragFloat3("POSITION", selected_map_object->mesh_instance->position);
                                        if (std::find(selected_map_object->base->config["data"].begin(), selected_map_object->base->config["data"].end(),
                                                "rotation") != selected_map_object->base->config["data"].end()
                                        ) {
                                                float rot_euler[3] = {
                                                        atan2(2*(selected_map_object->mesh_instance->rotation[3]*selected_map_object->mesh_instance->rotation[0] + selected_map_object->mesh_instance->rotation[1]*selected_map_object->mesh_instance->rotation[2]), 1 - 2*(selected_map_object->mesh_instance->rotation[0]*selected_map_object->mesh_instance->rotation[0] + selected_map_object->mesh_instance->rotation[1]*selected_map_object->mesh_instance->rotation[1])) * (float)(180/M_PI),
                                                        asin(2*(selected_map_object->mesh_instance->rotation[3]*selected_map_object->mesh_instance->rotation[1] - selected_map_object->mesh_instance->rotation[2]*selected_map_object->mesh_instance->rotation[0])) * (float)(180/M_PI),
                                                        atan2(2*(selected_map_object->mesh_instance->rotation[3]*selected_map_object->mesh_instance->rotation[2] + selected_map_object->mesh_instance->rotation[0]*selected_map_object->mesh_instance->rotation[1]), 1 - 2*(selected_map_object->mesh_instance->rotation[1]*selected_map_object->mesh_instance->rotation[1] + selected_map_object->mesh_instance->rotation[2]*selected_map_object->mesh_instance->rotation[2])) * (float)(180/M_PI)
                                                }; if (rot_euler[0] != rot_euler[0]) rot_euler[0] = 0.0f; if (rot_euler[1] != rot_euler[1]) rot_euler[1] = 0.0f; if (rot_euler[2] != rot_euler[2]) rot_euler[2] = 0.0f;  // (NaN fix)
                                                if (ImGui::DragFloat3("ROTATION", rot_euler, 1.0f, -180.0f, 180.0f)) {
                                                        float4 rot_quat = linalg::qmul(
                                                                linalg::rotation_quat(float3{0, 0, 1}, rot_euler[2]*(float)(M_PI/180)),
                                                                linalg::qmul(
                                                                        linalg::rotation_quat(float3{0, 1, 0}, rot_euler[1]*(float)(M_PI/180)),
                                                                        linalg::rotation_quat(float3{1, 0, 0}, rot_euler[0]*(float)(M_PI/180))
                                                                )
                                                        );
                                                        selected_map_object->mesh_instance->rotation[0] = rot_quat.x; selected_map_object->mesh_instance->rotation[1] = rot_quat.y; selected_map_object->mesh_instance->rotation[2] = rot_quat.z; selected_map_object->mesh_instance->rotation[3] = rot_quat.w;
                                                }
                                        }
                                        if (std::find(selected_map_object->base->config["data"].begin(), selected_map_object->base->config["data"].end(),
                                                "scale") != selected_map_object->base->config["data"].end()
                                        ) ImGui::DragFloat3("SCALE", selected_map_object->mesh_instance->scale);
                                        if (std::find(selected_map_object->base->config["data"].begin(), selected_map_object->base->config["data"].end(),
                                                "color") != selected_map_object->base->config["data"].end()
                                        ) {
                                                ImVec4 color = ImGui::ColorConvertU32ToFloat4(selected_map_object->mesh_instance->color_RGBA);
                                                float _temp;
                                                _temp = color.x; color.x = color.w; color.w = _temp;
                                                _temp = color.y; color.y = color.z; color.z = _temp;
                                                ImGui::ColorPicker4("COLOR", (float*)&color);
                                                _temp = color.x; color.x = color.w; color.w = _temp;
                                                _temp = color.y; color.y = color.z; color.z = _temp;
                                                selected_map_object->mesh_instance->color_RGBA = ImGui::ColorConvertFloat4ToU32(color);
                                        }
                                        ImGui::NewLine();
                                        uint64_t _uid = 0;
                                        for (auto& [label, val] : selected_map_object->extra_data.items()) { ImGui::PushID(_uid); switch (val.type()) { default: break;
                                                case (nlohmann::json::value_t::boolean): { bool _val = val;
                                                        ImGui::Checkbox(label.c_str(), &_val); val = _val;
                                                } break;
                                                case (nlohmann::json::value_t::number_float): { float _val = val;
                                                        ImGui::InputFloat(label.c_str(), &_val, 0, 0, "%f"); val = _val;
                                                } break;
                                                case (nlohmann::json::value_t::number_integer): { int64_t _val = val;
                                                        ImGui::InputScalar(label.c_str(), ImGuiDataType_S64, &_val); val = _val;
                                                } break;
                                                case (nlohmann::json::value_t::number_unsigned): { uint64_t _val = val;
                                                        ImGui::InputScalar(label.c_str(), ImGuiDataType_U64, &_val); val = _val;
                                                } break;
                                                case (nlohmann::json::value_t::string): { std::string _val = val;
                                                        ImGui::InputText(label.c_str(), &_val); val = _val;
                                                } break;
                                        } ImGui::PopID(); _uid++; }
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
                        for (const auto& map_object_instance : map_object_instances) {
                                nlohmann::json map_object_instance_json = nlohmann::json::object();
                                map_object_instance_json["name"] = map_object_instance->base->config["name"];
                                if (std::find(map_object_instance->base->config["data"].begin(), map_object_instance->base->config["data"].end(),
                                        "position") != map_object_instance->base->config["data"].end()
                                ) map_object_instance_json["position"] = nlohmann::json::array({map_object_instance->mesh_instance->position[0], map_object_instance->mesh_instance->position[1], map_object_instance->mesh_instance->position[2]});
                                if (std::find(map_object_instance->base->config["data"].begin(), map_object_instance->base->config["data"].end(),
                                        "rotation") != map_object_instance->base->config["data"].end()
                                ) map_object_instance_json["rotation"] = nlohmann::json::array({map_object_instance->mesh_instance->rotation[0], map_object_instance->mesh_instance->rotation[1], map_object_instance->mesh_instance->rotation[2], map_object_instance->mesh_instance->rotation[3]});
                                if (std::find(map_object_instance->base->config["data"].begin(), map_object_instance->base->config["data"].end(),
                                        "scale") != map_object_instance->base->config["data"].end()
                                ) map_object_instance_json["scale"] = nlohmann::json::array({map_object_instance->mesh_instance->scale[0], map_object_instance->mesh_instance->scale[1], map_object_instance->mesh_instance->scale[2]});
                                if (std::find(map_object_instance->base->config["data"].begin(), map_object_instance->base->config["data"].end(),
                                        "color") != map_object_instance->base->config["data"].end()
                                ) { map_object_instance_json["color"] = map_object_instance->mesh_instance->color_RGBA; }
                                if (map_object_instance->extra_data.size() > 0) { map_object_instance_json["extra_data"] = map_object_instance->extra_data; }
                                output.push_back(map_object_instance_json);
                        }
                        std::cout << std::setw(4) << output << std::endl;
                        break;
                }
        }
        ImGui::SaveIniSettingsToDisk(IMGUI_CACHE_PATH.c_str());
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }