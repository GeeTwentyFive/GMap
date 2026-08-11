#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>


#define ERROR(msg) throw std::runtime_error(std::string("[ERROR] ") + __FILE__ + "@" + std::to_string(__LINE__) + " (" + __func__ + "): " + (msg))


const std::string CONFIG_FILE = "GMap.json";

const nlohmann::json DEFAULT_CONFIG = nlohmann::json::parse(R"(
{
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
        "OBJECTS": [
                {}
        ]
}
)");

int main(int argc, char* argv[]) {
        if (!std::filesystem::is_regular_file(CONFIG_FILE)) { std::ofstream(CONFIG_FILE) << std::setw(4) << DEFAULT_CONFIG << std::endl; }

        nlohmann::json config; std::ifstream(CONFIG_FILE) >> config;

        // TEMP; TEST:
        std::cout << std::setw(4) << config << std::endl;

        return 0;
}