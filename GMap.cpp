#include <exception>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>
#include <vector>


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

const nlohmann::json MAPOBJECT_BASE = nlohmann::json::parse(R"({
        "CONFIG": {
                "model": "",
                "name": "",
                "extra_data": [],
                "override_translate": false,
                "override_rotate": false,
                "override_scale": false,
                "override_bounds": false,
                "override_coloring": false,
                "singleton": false
        },
        "MAP_DATA": {
                "pos": [0.0, 0.0, 0.0],
                "rot": [0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0]
        }
})");

// returns `""` on true, missing key on false
std::string JSONHasKeys(nlohmann::json json, std::vector<std::string> keys) { for (const std::string& key : keys) { if (!json.contains(key)) return key; }  return ""; }

int main(int argc, char* argv[]) { try { if (!std::filesystem::is_regular_file(CONFIG_FILE)) { std::ofstream(CONFIG_FILE) << std::setw(4) << DEFAULT_CONFIG << std::endl; throw std::runtime_error(CONFIG_FILE + " not found; new one has been created with global defaults. Populate its OBJECTS array with MapObjects"); }
        nlohmann::json config; std::ifstream(CONFIG_FILE) >> config; if (!config.contains("GLOBAL_SETTINGS")) throw std::runtime_error(CONFIG_FILE + " is missing key GLOBAL_SETTINGS"); if (!config.contains("OBJECTS")) throw std::runtime_error(CONFIG_FILE + " is missing key OBJECTS");
        std::string _result; _result = JSONHasKeys(config["GLOBAL_SETTINGS"], {"disable_bounds", "disable_coloring", "disable_rotate", "disable_scale", "disable_translate", "force_grid_snap", "x_max", "x_min", "y_max", "y_min", "z_max", "z_min"}); if (_result != "") throw std::runtime_error(CONFIG_FILE + " is missing key " + _result + " in GLOBAL_SETTINGS");
        if (config["OBJECTS"].size() == 0) throw std::runtime_error(CONFIG_FILE + " has no MapObjects in OBJECTS array");

        // TODO: Register map objects

        // TODO: nlohmann::json map_objects = nlohmann::json::array();

        // TODO: if (argc > 1): try to load map argv[1]
return 0; } catch (const std::exception& e) { std::cout << e.what() << std::endl; return 1; } }