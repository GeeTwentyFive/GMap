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


bool ValueInJSONArray(nlohmann::json value, nlohmann::json array) { return (std::find(array.begin(), array.end(), value) != array.end()); };

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
                registration->model = map_object["model"];
                if (map_object.contains("data")) {
                        //if (ValueInJSONArray("position", map_object["data"]))
                        // TODO
                }
                if (map_object.contains("extra_data")) registration->extra_data = map_object["extra_data"];
        }

        // TODO

        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }