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

#include <nlohmann/json.hpp>
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


#define ERROR(msg) throw std::runtime_error(std::string("[ERROR] ") + __FILE__ + "@" + std::to_string(__LINE__) + " (" + __func__ + "): " + (msg))


const int CAMERA_SENSITIVITY = 16;
const int STARTING_CAMERA_MOVE_SPEED = 8;

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

        GWindower gw{0, 0, 4, 6}; gw.locked_mouse = false; if (!gladLoadGL()) ERROR("Failed to load OpenGL functions");
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

        struct MapObjectInstance {  // Output: name, [pos], [rot], [scale], [color], [extra_data]
                uint64_t uid;
                MapObject* base;  // ^ name
                GRenderer3D::MeshInstance* mesh_instance;  // ^ pos, rot, scale, color
                nlohmann::json extra_data;  // ^ extra_data
        };
        std::vector<MapObjectInstance> map_object_instances;
        auto InstantiateMapObject = [&](
                std::string base,
                float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
                float quat_x = 0.0f, float quat_y = 0.0f, float quat_z = 0.0f, float quat_w = 1.0f,
                float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
                uint32_t color_shift_RGBA = 0xFFFFFFFF
        ) -> MapObjectInstance* { if (!registered_map_objects.contains(base)) ERROR(base + " not found in registered map objects");
                static uint64_t _last_uid = 0;
                MapObjectInstance* instance = &map_object_instances.emplace_back();
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
        auto RemoveMapObject = [&](uint64_t uid) {
                for (auto it = map_object_instances.begin(); it != map_object_instances.end(); it++) {
                        if (it->uid == uid) {
                                it->mesh_instance->Remove();
                                map_object_instances.erase(it); return;
                        }
                }
                ERROR(std::string("Failed to find & erase MapObjectInstance with uid ") + std::to_string(uid));
        };

        // TODO: if (argc > 1): try to load map argv[1]

        // TEMP; TEST:
        InstantiateMapObject("Cube");

        ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)(gw._glfwWindow), true); ImGui_ImplOpenGL3_Init("#version 460 core"); ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange);
        ImGui::GetIO().IniFilename = NULL; // TODO: imgui.ini in app(argv[0] end)-named dir in OS temp. dir
        // ^ OR: DockBuilder API

        glClearColor(0.0, 0.0, 0.0, 1.0);
        GFramePacer gfp; gfp.target_frametime_ms = 1.0;  // (frame limiting itself not needed because using v-sync (and wait-until-input), using lib for other frame-pacing-related benefits)
        while (gw.Update(true, 0.1, true)) {
                double delta_time = gfp.Wait();

                glViewport(0, 0, gw.window_width, gw.window_height);  // (leaving this inside of the mainloop in case I want to support windowed operation in the future)
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                // TODO: if (ImGui::GetIO().WantCaptureMouse): memset() clear GWindower's `mouse_button_states` & set `mouse_scroll_delta` to 0
                // TODO: if (ImGui::GetIO().WantCaptureKeyboard): memset() clear GWindower's `key_states`

                if (gw.mouse_button_states[GWindower::MOUSEBUTTON_RIGHT]) { gw.locked_mouse = true;
                        gr3d.camera_yaw += gw.mouse_x * CAMERA_SENSITIVITY;
                        gr3d.camera_pitch += gw.mouse_y * CAMERA_SENSITIVITY;
                }
                else { gw.locked_mouse = false;
                        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame(); ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

                        // TEMP; TEST:
                        ImGui::Begin("INSPECTOR", NULL, ImGuiWindowFlags_AlwaysAutoResize); {
                                ImGui::Text("TEST");
                        } ImGui::End();

                        // TODO

                        ImGui::Render();
                }

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

                gr3d.DrawFrame(gw.window_width, gw.window_height);
                if (!gw.mouse_button_states[GWindower::MOUSEBUTTON_RIGHT]) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


                if(gw.key_states[GWindower::KEY_ESCAPE]) {
                        // TODO: Serialize MapObjectInstances to JSON -> print to stdout

                        break;
                }
        }
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }