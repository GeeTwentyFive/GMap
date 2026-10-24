#include <climits>
#include <cstdint>
#include <cuchar>
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
#include <nfd.h>
static inline void _NFD_SetWaylandDisplay(void* wayland_display) {  // (to avoid using #ifdef below (since you can't stack C macros on the same lines as source code...))
#ifdef __linux__
        NFD_SetWaylandDisplay((struct wl_display*)wayland_display);
#endif
}
#include <GLineRenderer3D.hpp>
#include <libGMisc3DMath/libGMisc3DMath.hpp>
#include <libGGizmo3D/libGGizmo3D.hpp>


#define ERROR(msg) throw std::runtime_error(std::string("[ERROR] ") + __FILE__ + "@" + std::to_string(__LINE__) + " (" + __func__ + "): " + (msg))


const int CAMERA_SENSITIVITY = 16;
const int STARTING_CAMERA_MOVE_SPEED = 8;

const float POSITION_SNAP = 1.0f/64.0f;
const float ROTATION_SNAP = M_PI/64.0f;
const float SCALE_SNAP = 1.0f/64.0f;

const float GIZMO_WIDGET_LENGTH = 1.0f/4.0f;
const float GIZMO_WIDGET_WIDTH = 1.0f/32.0f;
const uint32_t GIZMO_WIDGET_COLORS[GGizmo3D::WidgetIndex::_count] = {
        0xFF0000FF, 0xAA0000FF, // X+, X-
        0x00FF00FF, 0x00AA00FF, // Y+, Y-
        0x0000FFFF, 0x000080FF  // Z+, Z-
};

const char USAGE_TEXT[] = ""
        "CONTROLS:\n"
        "- While right mouse button is held down:\n"
        "    - Mouse = rotate camera\n"
        "    - WASD, Space, CTRL = move camera\n"
        "    - Scroll = change camera move speed\n"
        "    - Click = select/deselect object\n"
        "    - 0 = reset camera position to 0, 0, 0\n"
        "- Del = delete selected object\n"
        "- Alt+C = clone selected object"
"";


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
                nlohmann::json extra_data = nlohmann::json::object();
        };
        static std::unordered_map<std::string, MapObjectRegistration> map_object_registrations; static std::vector<std::string> map_object_names; // (to keep same order as input registry in GUI)
        for (const auto& map_object : stdin_json_array) { if (!map_object.contains("name")) throw std::runtime_error("MapObject in stdin JSON array is missing key \"name\""); if (!map_object.contains("model")) throw std::runtime_error("MapObject in stdin JSON array is missing key \"model\"");
                if (map_object_registrations.contains(map_object["name"])) throw std::runtime_error(std::string("MapObject with name '") + std::string(map_object["name"]) + "' is already registered");
                MapObjectRegistration* registration = &map_object_registrations.emplace(map_object["name"], MapObjectRegistration{}).first->second;
                registration->name = map_object["name"]; map_object_names.push_back(registration->name);
                registration->model = std::filesystem::path(map_object["model"]).lexically_normal().generic_string();
                static auto IsValueInJSONArray = [](nlohmann::json value, nlohmann::json array) { return (std::find(array.begin(), array.end(), value) != array.end()); };
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

        static GWindower gw{1280, 720, 4, 6};
        if (!gladLoadGL()) ERROR("Failed to load OpenGL functions");

        GRenderer3D gr3d;

        auto GetMeshDataFromFile = [](std::string mesh_path) -> GOBJ* {
                static std::unordered_map<std::string, GOBJ> loaded_meshes;
                return &loaded_meshes.try_emplace(mesh_path, mesh_path).first->second;
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
                        const std::vector<GOBJ::Vertex>* vertices = &GetMeshDataFromFile(mesh_path)->vertices;
                        calculated_aabbs[mesh_path] = gcollision2::AABB::FromVertices((float*)vertices->data(), vertices->size(), offsetof(GOBJ::Vertex, pos), sizeof(GOBJ::Vertex));
                }
                return &calculated_aabbs[mesh_path];
        };

        if (NFD_Init() != NFD_OKAY) ERROR(NFD_GetError()); _NFD_SetWaylandDisplay(gw.native_wayland_display);

        static auto ShowAndGetPathFromOpenDialog = [](std::string target_file_type_extension = "") -> std::string {
                std::string OUT_path = "";
                char* path;
                nfdopendialogu8args_t args{};
                if (target_file_type_extension != "") {
                        static nfdu8filteritem_t filter = {target_file_type_extension.c_str(), target_file_type_extension.c_str()};
                        args.filterList = &filter; args.filterCount = 1;
                }
                std::string _current_path = std::filesystem::current_path().string();
                args.defaultPath = _current_path.c_str();
                args.parentWindow.handle = gw.native_window_handle;
                #ifdef __linux__
                        args.parentWindow.type = NFD_WINDOW_HANDLE_TYPE_WAYLAND;
                #else
                        args.parentWindow.type = NFD_WINDOW_HANDLE_TYPE_WINDOWS;
                #endif
                if (NFD_OpenDialogU8_With(&path, &args) == NFD_OKAY) {
                        OUT_path = path;
                        NFD_FreePathU8(path);
                }
                return OUT_path;
        };

        struct MapObjectInstance;
        static std::vector<std::unique_ptr<MapObjectInstance>> map_object_instances;
        struct MapObjectInstance {
                MapObjectRegistration* base_registration;  // name & model
                GRenderer3D::MeshInstance* mesh_instance;  // position, rotation, scale, & color
                std::string user_model = "";
                std::string user_texture = "";
                nlohmann::json extra_data = nlohmann::json::object();

                static MapObjectInstance* Create(
                        std::string name,
                        float pos_x = 0.0f, float pos_y = 0.0f, float pos_z = 0.0f,
                        float rot_x = 0.0f, float rot_y = 0.0f, float rot_z = 0.0f, float rot_w = 1.0f,
                        float scale_x = 1.0f, float scale_y = 1.0f, float scale_z = 1.0f,
                        uint32_t color_shift_RGBA = 0xFFFFFFFF,
                        std::string user_model = "",
                        std::string user_texture = "",
                        nlohmann::json extra_data = nlohmann::json::object()
                ) { if (!map_object_registrations.contains(name)) throw std::runtime_error(std::string("ERROR: No registered MapObject found with name '") + name + "'");
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
                        if (extra_data.empty()) extra_data = instance->base_registration->extra_data;
                        instance->extra_data = extra_data;
                        return instance;
                } private: MapObjectInstance(){}; public:

                void SetUserModelAndOrTexture(std::string model_path = "", std::string texture_path = "") { if (!base_registration->data.user_model && !base_registration->data.user_texture) return; if (model_path == "" && texture_path == "") return;
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
                } std::string GetUserModel() { return user_model; } std::string GetUserTexture() { return user_texture; }

                void SetPosition(float3 pos) { if (!base_registration->data.position) return;
                        pos = linalg::round(pos / POSITION_SNAP) * POSITION_SNAP;
                        *(float3*)mesh_instance->position = pos;
                }

                void SetQuat(float4 quat) { if (!base_registration->data.rotation) return;
                        *(float4*)mesh_instance->rotation = quat;
                }
                float3 GetEuler() { return GMisc3DMath::QuatToEuler(*(float4*)mesh_instance->rotation); }
                void SetEuler(float3 euler) { if (!base_registration->data.rotation) return;
                        *(float4*)mesh_instance->rotation = GMisc3DMath::EulerToQuat(euler);
                }

                void SetScale(float3 scale) { if (!base_registration->data.scale) return;
                        scale = linalg::round(scale / SCALE_SNAP) * SCALE_SNAP;
                        *(float3*)mesh_instance->scale = scale;
                }

                void SetColor(uint32_t color_RGBA) { if (!base_registration->data.color) return;
                        mesh_instance->color_RGBA = color_RGBA;
                }

                gcollision2::AABB* LocalAABB() { return (user_model != "") ? GetMeshAABBFromFile(user_model) : GetMeshAABBFromFile(base_registration->model); }
                gcollision2::AABB WorldAABB() { return LocalAABB()->Transformed(mesh_instance->Transform()); }

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
                static MapObjectInstance* CreateFromJSON(nlohmann::json map_object_instance_json) { if (!map_object_instance_json.is_object()) ERROR("Passed MapObject instance JSON is not a JSON object (it has to be curly braces)");
                        std::string name = map_object_instance_json.at("name");

                        if (!map_object_registrations.contains(name)) throw std::runtime_error(std::string("ERROR: No registered MapObject found with name '") + name + "'");
                        const MapObjectRegistration* registration = &map_object_registrations[name];

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

                        float scale_x = 1; float scale_y = 1; float scale_z = 1;
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
                        if (registration->extra_data.size() > 0) extra_data = map_object_instance_json.at("extra_data");

                        return Create(
                                name,
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
                for (size_t i = 0; i < input.size(); i++) { try { MapObjectInstance::CreateFromJSON(input[i]); } catch (const std::exception& e) { throw std::runtime_error(std::string("ERROR: Failed to load MapObject instance ") + std::to_string(i) + " from '"+argv[1]+"':\n" + input[i].dump(4) + "\n\n" + e.what()); } }
        }

        ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)(gw._glfwWindow), true); ImGui_ImplOpenGL3_Init("#version 460 core"); ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_NoMouseCursorChange); ImGui::GetIO().IniFilename = NULL;

        GLineRenderer3D glr3d;

        MapObjectInstance* _selected_map_object = nullptr;  // (not set directly (but via SetSelectedMapObject()) to make sure `selected_map_object_just_changed` is updated/correct)
        bool selected_map_object_just_changed = false;
        auto SetSelectedMapObject = [&](MapObjectInstance* target = nullptr) { _selected_map_object = target; selected_map_object_just_changed = true; };
        auto GetSelectedMapObject = [&]() -> MapObjectInstance* { return _selected_map_object; };

        glClearColor(0.0, 0.0, 0.0, 1.0);
        GFramePacer gfp; gfp.target_frametime_ms = 1.0;  // (frame limiting itself not needed because using v-sync (and wait-until-input), using lib for other frame-pacing-related benefits)
        for (;;) { // MAIN LOOP
                double delta_time = gfp.Wait();

                const bool controlling_camera = gw.input_states[GWindower::MOUSEBUTTON_RIGHT];

                bool window_should_close = !gw.Update(controlling_camera, false, 0.0, true);

                static int camera_move_speed = STARTING_CAMERA_MOVE_SPEED;

                if (controlling_camera) {
                        if (gw.mouse_scroll_delta > 0.5) camera_move_speed *= 2;
                        if (gw.mouse_scroll_delta < -0.5) camera_move_speed /= 2;
                        if (camera_move_speed < 1) camera_move_speed = 1;

                        gr3d.camera_yaw += gw.mouse_x_delta * CAMERA_SENSITIVITY;
                        gr3d.camera_pitch += gw.mouse_y_delta * CAMERA_SENSITIVITY;
                        gr3d.camera_pos[1] += (-gw.input_states[GWindower::KEY_LEFT_CONTROL] + gw.input_states[GWindower::KEY_SPACE]) * camera_move_speed * delta_time;
                        float2 move_dir = float2{
                                (float)(-gw.input_states[GWindower::KEY_A] + gw.input_states[GWindower::KEY_D]),
                                (float)(-gw.input_states[GWindower::KEY_W] + gw.input_states[GWindower::KEY_S])
                        };
                        if (linalg::length2(move_dir) != 0) move_dir = linalg::normalize(move_dir);  // (to avoid division by zero)
                        move_dir = linalg::rot((float)(gr3d.camera_yaw*(2*M_PI/65536)), move_dir);
                        gr3d.camera_pos[0] += -move_dir.x * camera_move_speed * delta_time;
                        gr3d.camera_pos[2] += -move_dir.y * camera_move_speed * delta_time;

                        if (gw.input_states[GWindower::KEY_ZERO]) { gr3d.camera_pos[0] = 0; gr3d.camera_pos[1] = 0; gr3d.camera_pos[2] = 0; }
                }

                static bool show_ui = true;

                if (gw.input_states[GWindower::KEY_LEFT_ALT] && gw.input_states[GWindower::KEY_H] && (gw.last_input_states[GWindower::KEY_H] == false)) show_ui = !show_ui;

                static GGizmo3D::WidgetIndex active_gizmo_widget = GGizmo3D::WidgetIndex::NONE;

                if (show_ui) {
                        if (GetSelectedMapObject() != nullptr && (GetSelectedMapObject()->base_registration->data.position || GetSelectedMapObject()->base_registration->data.rotation || GetSelectedMapObject()->base_registration->data.scale)) {
                                const float gizmo_scale = linalg::length(*(float3*)GetSelectedMapObject()->mesh_instance->position - *(float3*)gr3d.camera_pos);
                                GGizmo3D gizmo(
                                        *(float3*)GetSelectedMapObject()->mesh_instance->position,
                                        GIZMO_WIDGET_LENGTH * gizmo_scale, GIZMO_WIDGET_WIDTH * gizmo_scale
                                );
                                const bool select_gizmo = gw.input_states[GWindower::MOUSEBUTTON_LEFT];
                                const bool select_gizmo_just_pressed = select_gizmo && (gw.last_input_states[GWindower::MOUSEBUTTON_LEFT] == false);
                                if (!select_gizmo || controlling_camera) active_gizmo_widget = GGizmo3D::WidgetIndex::NONE;
                                else if (select_gizmo_just_pressed) {
                                        active_gizmo_widget = gizmo.Pick(
                                                *(float3*)gr3d.camera_pos,
                                                GMisc3DMath::ScreenToWorldRay(gw.cursor_x, gw.cursor_y, gw.window_width, gw.window_height, (float4x4*)gr3d.ViewProjection())
                                        );
                                }
                                const bool rotate = gw.input_states[GWindower::KEY_LEFT_SHIFT];
                                const bool scale = gw.input_states[GWindower::KEY_LEFT_ALT];
                                static auto DrawGizmoWidget = [&](GGizmo3D::WidgetIndex widget_index) { glr3d.DrawBox(gizmo.aabbs[widget_index].min.x, gizmo.aabbs[widget_index].min.y, gizmo.aabbs[widget_index].min.z, gizmo.aabbs[widget_index].max.x, gizmo.aabbs[widget_index].max.y, gizmo.aabbs[widget_index].max.z, GIZMO_WIDGET_COLORS[widget_index]); };
                                switch (active_gizmo_widget) { default: { for (int i = 0; i < GGizmo3D::WidgetIndex::_count; i++) { DrawGizmoWidget((GGizmo3D::WidgetIndex)i); } } break;
                                        case GGizmo3D::WidgetIndex::Y_POSITIVE: case GGizmo3D::WidgetIndex::Y_NEGATIVE: { DrawGizmoWidget(GGizmo3D::WidgetIndex::Y_POSITIVE); DrawGizmoWidget(GGizmo3D::WidgetIndex::Y_NEGATIVE);
                                                if (rotate) GetSelectedMapObject()->SetQuat(linalg::qmul(
                                                        linalg::rotation_quat({0, 1, 0}, gw.mouse_x_delta * -ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation
                                                ));
                                                else if (scale) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{0, (-gw.mouse_y_delta) * SCALE_SNAP, 0});
                                                else {
                                                        const float delta = (-gw.mouse_y_delta) * POSITION_SNAP;
                                                        GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{0, delta, 0}); gr3d.camera_pos[1] += delta;
                                                }
                                        } break;
                                        case GGizmo3D::WidgetIndex::X_POSITIVE: case GGizmo3D::WidgetIndex::X_NEGATIVE: { DrawGizmoWidget(GGizmo3D::WidgetIndex::X_POSITIVE); DrawGizmoWidget(GGizmo3D::WidgetIndex::X_NEGATIVE);
                                                if (rotate) {
                                                        float delta = (-gw.mouse_y_delta) * ROTATION_SNAP; if (gr3d.camera_yaw > (USHRT_MAX/4)*1 && gr3d.camera_yaw < (USHRT_MAX/4)*3) delta = -delta;
                                                        GetSelectedMapObject()->SetQuat(linalg::qmul(
                                                                linalg::rotation_quat({1, 0, 0}, delta), *(float4*)GetSelectedMapObject()->mesh_instance->rotation
                                                        ));
                                                } else if (scale) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{gw.mouse_x_delta * SCALE_SNAP, 0, 0});
                                                else {
                                                        float delta = gw.mouse_x_delta * POSITION_SNAP; if (gr3d.camera_yaw > (USHRT_MAX/4)*1 && gr3d.camera_yaw < (USHRT_MAX/4)*3) delta = -delta;
                                                        GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{-delta, 0, 0}); gr3d.camera_pos[0] -= delta;
                                                }
                                        } break;
                                        case GGizmo3D::WidgetIndex::Z_POSITIVE: case GGizmo3D::WidgetIndex::Z_NEGATIVE: { DrawGizmoWidget(GGizmo3D::WidgetIndex::Z_POSITIVE); DrawGizmoWidget(GGizmo3D::WidgetIndex::Z_NEGATIVE);
                                                if (rotate) {
                                                        float delta = (-gw.mouse_y_delta) * ROTATION_SNAP; if (gr3d.camera_yaw > (USHRT_MAX/4)*0 && gr3d.camera_yaw < (USHRT_MAX/4)*2) delta = -delta;
                                                        GetSelectedMapObject()->SetQuat(linalg::qmul(
                                                                linalg::rotation_quat({0, 0, 1}, -delta), *(float4*)GetSelectedMapObject()->mesh_instance->rotation
                                                        ));
                                                } else if (scale) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{0, 0, gw.mouse_x_delta * SCALE_SNAP});
                                                else {
                                                        float delta = gw.mouse_x_delta * POSITION_SNAP; if (gr3d.camera_yaw > (USHRT_MAX/4)*0 && gr3d.camera_yaw < (USHRT_MAX/4)*2) delta = -delta;
                                                        GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{0, 0, delta}); gr3d.camera_pos[2] += delta;
                                                }
                                        } break;
                                }
                        }
                }

                if (gw.input_states[GWindower::MOUSEBUTTON_LEFT] && (gw.last_input_states[GWindower::MOUSEBUTTON_LEFT] == false) && !controlling_camera && (active_gizmo_widget == GGizmo3D::WidgetIndex::NONE) && !ImGui::GetIO().WantCaptureMouse) {
                        MapObjectInstance* closest_hit_map_object = nullptr;
                        float closest_hit_map_object_distance = FLT_MAX;
                        for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) {
                                gcollision2::AABB map_object_instance_aabb = map_object_instance->WorldAABB();
                                float hit_map_object_distance;
                                if (gcollision2::IntersectRayAABB(
                                        float3{gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]},
                                        GMisc3DMath::ScreenToWorldRay(gw.cursor_x, gw.cursor_y, gw.window_width, gw.window_height, (float4x4*)gr3d.ViewProjection()),
                                        map_object_instance_aabb, &hit_map_object_distance
                                )) if (hit_map_object_distance < closest_hit_map_object_distance) { closest_hit_map_object_distance = hit_map_object_distance;
                                        closest_hit_map_object = map_object_instance.get();
                                }
                        }
                        if (closest_hit_map_object_distance != FLT_MAX) {
                                if (closest_hit_map_object != GetSelectedMapObject()) SetSelectedMapObject(closest_hit_map_object);
                        }
                        else SetSelectedMapObject();  // (none hit; deselect)
                }

                if (GetSelectedMapObject() != nullptr) {
                        if (gw.input_states[GWindower::KEY_LEFT_ALT] && gw.input_states[GWindower::KEY_C] && (gw.last_input_states[GWindower::KEY_C] == false) && GetSelectedMapObject() != nullptr) SetSelectedMapObject(GetSelectedMapObject()->Clone());

                        if (gw.input_states[GWindower::KEY_DELETE] && GetSelectedMapObject() != nullptr) { GetSelectedMapObject()->Delete(); SetSelectedMapObject(); }

                        if (gw.input_states[GWindower::MOUSEBUTTON_MIDDLE]) {
                                if (gw.input_states[GWindower::KEY_LEFT_ALT] && GetSelectedMapObject()->base_registration->data.rotation) {  // lookat
                                        float3 target_point;
                                        float closest_hit_map_object_distance = FLT_MAX;
                                        for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) { if (map_object_instance.get() == GetSelectedMapObject()) continue;  // (ignore selected obj)
                                                gcollision2::AABB map_object_instance_aabb = map_object_instance->WorldAABB();
                                                float hit_map_object_distance;
                                                float3 hit_map_object_point;
                                                if (gcollision2::IntersectRayAABB(
                                                        float3{gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]},
                                                        GMisc3DMath::ScreenToWorldRay(gw.cursor_x, gw.cursor_y, gw.window_width, gw.window_height, (float4x4*)gr3d.ViewProjection()),
                                                        map_object_instance_aabb, &hit_map_object_distance,
                                                        &hit_map_object_point
                                                )) if (hit_map_object_distance < closest_hit_map_object_distance) { closest_hit_map_object_distance = hit_map_object_distance;
                                                        target_point = hit_map_object_point;
                                                }
                                        }
                                        if (closest_hit_map_object_distance != FLT_MAX) {
                                                const float4x4 lookat_matrix = linalg::lookat_matrix(
                                                        float3{GetSelectedMapObject()->mesh_instance->position[0], GetSelectedMapObject()->mesh_instance->position[1], GetSelectedMapObject()->mesh_instance->position[2]},
                                                        target_point, float3{0, 1, 0}, linalg::fwd_axis::pos_z
                                                );
                                                GetSelectedMapObject()->SetQuat(linalg::rotation_quat(float3x3{lookat_matrix.row(0).xyz(), lookat_matrix.row(1).xyz(), lookat_matrix.row(2).xyz()}));
                                        }
                                }
                                else if (GetSelectedMapObject()->base_registration->data.position) {  // surface snap
                                        float3 target_point, target_normal;
                                        float closest_hit_map_object_distance = FLT_MAX;
                                        for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) { if (map_object_instance.get() == GetSelectedMapObject()) continue;  // (ignore selected obj)
                                                gcollision2::AABB map_object_instance_aabb = map_object_instance->WorldAABB();
                                                float hit_map_object_distance;
                                                float3 hit_map_object_point, hit_map_object_normal; bool ray_inside;
                                                if (gcollision2::IntersectRayAABB(
                                                        float3{gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]},
                                                        GMisc3DMath::ScreenToWorldRay(gw.cursor_x, gw.cursor_y, gw.window_width, gw.window_height, (float4x4*)gr3d.ViewProjection()),
                                                        map_object_instance_aabb, &hit_map_object_distance,
                                                        &hit_map_object_point, &hit_map_object_normal, &ray_inside
                                                )) if (hit_map_object_distance < closest_hit_map_object_distance) { closest_hit_map_object_distance = hit_map_object_distance;
                                                        target_point = hit_map_object_point;
                                                        target_normal = (ray_inside ? -hit_map_object_normal : hit_map_object_normal);
                                                }
                                        }
                                        if (closest_hit_map_object_distance != FLT_MAX) {
                                                const float3 align_offset = (target_normal * GetSelectedMapObject()->WorldAABB().GetSize()/2.0f) - GetSelectedMapObject()->LocalAABB()->GetCenterPos();
                                                target_point = linalg::round(target_point / POSITION_SNAP) * POSITION_SNAP;
                                                GetSelectedMapObject()->mesh_instance->position[0] = target_point.x + align_offset.x; GetSelectedMapObject()->mesh_instance->position[1] = target_point.y + align_offset.y; GetSelectedMapObject()->mesh_instance->position[2] = target_point.z + align_offset.z;
                                        }
                                }
                        }

                        const float camera_yaw_offsetted_45 = (uint16_t)(gr3d.camera_yaw + USHRT_MAX/8); // offsetting by 45-degrees (PI/4) to make it so the axes line up neatly for conditional checks (otherwise -Z (angle 0 (forward)) axis alignment check would be both less than 0 *and* greater than 0) (...might make more sense if you visualize it in your head (starts off as like a pizza sliced into 4 slices making an 'X' shape, then rotated 45-degrees to make the slice seams into a '+' shape))  // (re-casting to ushort to force ushort overflow)

                        const bool up_just_pressed = gw.input_states[GWindower::KEY_UP] && (gw.last_input_states[GWindower::KEY_UP] == false);
                        const bool down_just_pressed = gw.input_states[GWindower::KEY_DOWN] && (gw.last_input_states[GWindower::KEY_DOWN] == false);
                        if (up_just_pressed || down_just_pressed) {
                                const float delta = -(float)down_just_pressed + (float)up_just_pressed;
                                if (gw.input_states[GWindower::KEY_LEFT_SHIFT]) {
                                        if (camera_yaw_offsetted_45 > 0 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*1) GetSelectedMapObject()->SetQuat(linalg::qmul(linalg::rotation_quat({delta, 0, 0}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation));
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*1 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*2) GetSelectedMapObject()->SetQuat(linalg::qmul(linalg::rotation_quat({0, 0, delta}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation));
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*2 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*3) GetSelectedMapObject()->SetQuat(linalg::qmul(linalg::rotation_quat({-delta, 0, 0}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation));
                                        else GetSelectedMapObject()->SetQuat(linalg::qmul(linalg::rotation_quat({0, 0, -delta}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation));
                                }
                                else if (gw.input_states[GWindower::KEY_LEFT_ALT]) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{0, delta * SCALE_SNAP, 0});
                                else GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{0, delta * POSITION_SNAP, 0});
                        }
                        const bool right_just_pressed = gw.input_states[GWindower::KEY_RIGHT] && (gw.last_input_states[GWindower::KEY_RIGHT] == false);
                        const bool left_just_pressed = gw.input_states[GWindower::KEY_LEFT] && (gw.last_input_states[GWindower::KEY_LEFT] == false);
                        if (right_just_pressed || left_just_pressed) {
                                const float delta = -(float)left_just_pressed + (float)right_just_pressed;
                                if (gw.input_states[GWindower::KEY_LEFT_SHIFT]) GetSelectedMapObject()->SetQuat(linalg::qmul(linalg::rotation_quat({0, delta, 0}, -ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation));
                                else if (gw.input_states[GWindower::KEY_LEFT_ALT]) {
                                        if (camera_yaw_offsetted_45 > 0 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*1) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{delta * SCALE_SNAP, 0, 0});
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*1 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*2) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{0, 0, delta * SCALE_SNAP});
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*2 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*3) GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{delta * SCALE_SNAP, 0, 0});
                                        else GetSelectedMapObject()->SetScale(*(float3*)GetSelectedMapObject()->mesh_instance->scale + float3{0, 0, delta * SCALE_SNAP});
                                }
                                else { if (GetSelectedMapObject()->base_registration->data.position) {
                                        if (camera_yaw_offsetted_45 > 0 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*1) GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{-delta * POSITION_SNAP, 0, 0});
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*1 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*2) GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{0, 0, -delta * POSITION_SNAP});
                                        else if (camera_yaw_offsetted_45 > (USHRT_MAX/4)*2 && camera_yaw_offsetted_45 < (USHRT_MAX/4)*3) GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{delta * POSITION_SNAP, 0, 0});
                                        else GetSelectedMapObject()->SetPosition(*(float3*)GetSelectedMapObject()->mesh_instance->position + float3{0, 0, delta * POSITION_SNAP});
                                }}
                        }
                }

                static bool show_quit_dialog = false;

                if (gw.input_states[GWindower::KEY_ESCAPE] && (gw.last_input_states[GWindower::KEY_ESCAPE] == false)) show_quit_dialog = !show_quit_dialog;

                if (window_should_close) show_quit_dialog = true;

                if (show_quit_dialog) show_ui = true;

                if (show_ui) { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame(); if (controlling_camera || (gw.input_states[GWindower::MOUSEBUTTON_LEFT] && !ImGui::GetIO().WantCaptureMouse)) ImGui::SetWindowFocus(); /* (unfocus GUI if controlling camera or clicked off) */ }

                if (show_ui) { if (GetSelectedMapObject() != nullptr) { const gcollision2::AABB aabb = GetSelectedMapObject()->WorldAABB(); glr3d.DrawBox(aabb.min.x, aabb.min.y, aabb.min.z, aabb.max.x, aabb.max.y, aabb.max.z); }
                        ImGui::SetNextWindowPos({0.0f, (float)gw.window_height}, ImGuiCond_Always, {0.0f, 1.0f}); ImGui::Begin("USAGE", NULL, (ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); { ImGui::TextUnformatted(USAGE_TEXT, USAGE_TEXT+sizeof(USAGE_TEXT)); } ImGui::End();

                        ImGui::SetNextWindowPos({(float)gw.window_width, 0.0f}, ImGuiCond_Always, {1.0f, 0.0f}); ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {gw.window_width/3.0f, FLT_MAX});
                        ImGui::Begin("ADD", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                for (const std::string& name : map_object_names) {
                                        if (ImGui::Button(name.c_str())) SetSelectedMapObject(MapObjectInstance::Create(name, gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]));
                                }
                        } ImGui::End();

                        if (GetSelectedMapObject() != nullptr && !selected_map_object_just_changed) {
                                ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always, {0.0f, 0.0f}); ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {gw.window_width/3.0f, FLT_MAX});
                                ImGui::Begin("INSPECTOR", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                        if (GetSelectedMapObject()->base_registration->data.user_model) {
                                                static const char button_label[] = "Model";
                                                if (ImGui::Button(button_label)) GetSelectedMapObject()->SetUserModelAndOrTexture(ShowAndGetPathFromOpenDialog("obj"));
                                                std::string path_label_text = GetSelectedMapObject()->GetUserModel();
                                                if (path_label_text.size() > 32-sizeof(button_label)) path_label_text = "..." + path_label_text.substr(path_label_text.size() - (32-sizeof(button_label)));
                                                ImGui::SameLine();
                                                ImGui::TextUnformatted(path_label_text.c_str(), path_label_text.c_str()+path_label_text.size());
                                        }
                                        if (GetSelectedMapObject()->base_registration->data.user_texture) {
                                                static const char button_label[] = "Texture";
                                                if (ImGui::Button(button_label)) GetSelectedMapObject()->SetUserModelAndOrTexture("", ShowAndGetPathFromOpenDialog("png"));
                                                std::string path_label_text = GetSelectedMapObject()->GetUserTexture();
                                                if (path_label_text.size() > 32-sizeof(button_label)) path_label_text = "..." + path_label_text.substr(path_label_text.size() - (32-sizeof(button_label)));
                                                ImGui::SameLine();
                                                ImGui::TextUnformatted(path_label_text.c_str(), path_label_text.c_str()+path_label_text.size());
                                        }
                                        if (GetSelectedMapObject()->base_registration->data.position) {
                                                float previous_position[3] = {GetSelectedMapObject()->mesh_instance->position[0], GetSelectedMapObject()->mesh_instance->position[1], GetSelectedMapObject()->mesh_instance->position[2]};
                                                if (ImGui::DragFloat3("POSITION", GetSelectedMapObject()->mesh_instance->position, POSITION_SNAP, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoSpeedTweaks)) {
                                                        if (GetSelectedMapObject()->mesh_instance->position[0] != previous_position[0]) GetSelectedMapObject()->mesh_instance->position[0] = std::round(GetSelectedMapObject()->mesh_instance->position[0] / POSITION_SNAP) * POSITION_SNAP;
                                                        if (GetSelectedMapObject()->mesh_instance->position[1] != previous_position[1]) GetSelectedMapObject()->mesh_instance->position[1] = std::round(GetSelectedMapObject()->mesh_instance->position[1] / POSITION_SNAP) * POSITION_SNAP;
                                                        if (GetSelectedMapObject()->mesh_instance->position[2] != previous_position[2]) GetSelectedMapObject()->mesh_instance->position[2] = std::round(GetSelectedMapObject()->mesh_instance->position[2] / POSITION_SNAP) * POSITION_SNAP;
                                                }
                                        }
                                        if (GetSelectedMapObject()->base_registration->data.rotation) {
                                                float3 euler = GetSelectedMapObject()->GetEuler();
                                                float3 previous_euler = euler;
                                                if (ImGui::DragFloat3("ROTATION", (float*)&euler, 1.0f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoSpeedTweaks)) {
                                                        if (gw.input_states[GWindower::MOUSEBUTTON_LEFT] && (gw.last_input_states[GWindower::MOUSEBUTTON_LEFT] != false)) {
                                                                if (euler.x != previous_euler.x) *(float4*)GetSelectedMapObject()->mesh_instance->rotation = linalg::qmul(linalg::rotation_quat({(euler.x - previous_euler.x), 0, 0}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation);
                                                                if (euler.y != previous_euler.y) *(float4*)GetSelectedMapObject()->mesh_instance->rotation = linalg::qmul(linalg::rotation_quat({0, (euler.y - previous_euler.y), 0}, -ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation);
                                                                if (euler.z != previous_euler.z) *(float4*)GetSelectedMapObject()->mesh_instance->rotation = linalg::qmul(linalg::rotation_quat({0, 0, (euler.z - previous_euler.z)}, ROTATION_SNAP), *(float4*)GetSelectedMapObject()->mesh_instance->rotation);
                                                        } else GetSelectedMapObject()->SetEuler(euler);
                                                }
                                        }
                                        if (GetSelectedMapObject()->base_registration->data.scale) {
                                                float previous_scale[3] = {GetSelectedMapObject()->mesh_instance->scale[0], GetSelectedMapObject()->mesh_instance->scale[1], GetSelectedMapObject()->mesh_instance->scale[2]};
                                                if (ImGui::DragFloat3("SCALE", GetSelectedMapObject()->mesh_instance->scale, SCALE_SNAP, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoSpeedTweaks)) {
                                                        if (GetSelectedMapObject()->mesh_instance->scale[0] != previous_scale[0]) GetSelectedMapObject()->mesh_instance->scale[0] = std::round(GetSelectedMapObject()->mesh_instance->scale[0] / SCALE_SNAP) * SCALE_SNAP;
                                                        if (GetSelectedMapObject()->mesh_instance->scale[1] != previous_scale[1]) GetSelectedMapObject()->mesh_instance->scale[1] = std::round(GetSelectedMapObject()->mesh_instance->scale[1] / SCALE_SNAP) * SCALE_SNAP;
                                                        if (GetSelectedMapObject()->mesh_instance->scale[2] != previous_scale[2]) GetSelectedMapObject()->mesh_instance->scale[2] = std::round(GetSelectedMapObject()->mesh_instance->scale[2] / SCALE_SNAP) * SCALE_SNAP;
                                                }
                                        }
                                        if (GetSelectedMapObject()->base_registration->data.color) {
                                                ImVec4 color = ImGui::ColorConvertU32ToFloat4(GetSelectedMapObject()->mesh_instance->color_RGBA);
                                                float _temp;
                                                _temp = color.x; color.x = color.w; color.w = _temp;
                                                _temp = color.y; color.y = color.z; color.z = _temp;
                                                ImGui::ColorPicker4("COLOR", (float*)&color);
                                                _temp = color.x; color.x = color.w; color.w = _temp;
                                                _temp = color.y; color.y = color.z; color.z = _temp;
                                                GetSelectedMapObject()->mesh_instance->color_RGBA = ImGui::ColorConvertFloat4ToU32(color);
                                        }
                                        if (GetSelectedMapObject()->extra_data.size() > 0) { ImGui::NewLine();
                                                uint64_t _uid = 0;
                                                for (auto& [label, val] : GetSelectedMapObject()->extra_data.items()) { ImGui::PushID(_uid); switch (val.type()) { default: break;
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
                                } ImGui::End();
                        }

                        static bool show_extra_info = false;
                        if (gw.input_states[GWindower::KEY_LEFT_ALT] && gw.input_states[GWindower::KEY_I] && (gw.last_input_states[GWindower::KEY_I] == false)) show_extra_info = !show_extra_info;
                        if (show_extra_info) {
                                ImGui::SetNextWindowPos({(float)gw.window_width/2, (float)gw.window_height}, ImGuiCond_Always, {0.5f, 1.0f}); ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {gw.window_width/3.0f, FLT_MAX});
                                ImGui::Begin("INFO", NULL, (ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                        ImGui::Text("%.3f %.3f %.3f", gr3d.camera_pos[0], gr3d.camera_pos[1], gr3d.camera_pos[2]);
                                } ImGui::End();
                        }

                        if (show_quit_dialog) {
                                ImGui::SetNextWindowPos({(float)gw.window_width/2.0f, (float)gw.window_height/2.0f}, ImGuiCond_Always, {0.5f, 0.5f});
                                ImGui::Begin("QUIT_DIALOG", NULL, (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)); {
                                        if (ImGui::Button("Save & Quit")) {
                                                nlohmann::json output = nlohmann::json::array();
                                                for (const std::unique_ptr<MapObjectInstance>& map_object_instance : map_object_instances) {
                                                        output.push_back(map_object_instance->SerializeToJSON());
                                                }
                                                std::cout << std::setw(4) << output << std::endl;
                                                break;
                                        }
                                        if (ImGui::Button("Quit without saving")) break;
                                        if (ImGui::Button("Cancel")) show_quit_dialog = false;
                                } ImGui::End();
                        }
                }

                selected_map_object_just_changed = false;

                glViewport(0, 0, gw.window_width, gw.window_height);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                gr3d.DrawFrame(gw.window_width, gw.window_height);
                if (show_ui) {
                        glClear(GL_DEPTH_BUFFER_BIT);
                        glr3d.camera_pos[0] = gr3d.camera_pos[0]; glr3d.camera_pos[1] = gr3d.camera_pos[1]; glr3d.camera_pos[2] = gr3d.camera_pos[2]; glr3d.camera_yaw = gr3d.camera_yaw; glr3d.camera_pitch = gr3d.camera_pitch; glr3d.camera_fov = gr3d.camera_fov; glr3d.camera_near = gr3d.camera_near; glr3d.camera_far = gr3d.camera_far;
                        glr3d.DrawFrame(gw.window_width, gw.window_height);
                        glClear(GL_DEPTH_BUFFER_BIT);
                        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                }
        }
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); NFD_Quit();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }