#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <json_config_file>" << std::endl;
        return 1;
    }

    std::string config_file_path = argv[1];
    std::ifstream config_file(config_file_path);

    if (!config_file.is_open()) {
        std::cerr << "Error: Could not open config file " << config_file_path << std::endl;
        return 1;
    }

    json config_data;
    try {
        config_file >> config_data;
    } catch (const json::parse_error& e) {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        return 1;
    }

    std::string output_string;

    if (config_data.contains("suppress_apps") && config_data["suppress_apps"].is_object()) {
        for (auto const& [app_package, app_config] : config_data["suppress_apps"].items()) {
            if (app_config.contains("enabled") && app_config["enabled"].is_boolean() && app_config["enabled"].get<bool>()) {
                // Add package name if enabled
                output_string += app_package + " ";

                // Add processes if they exist and are an array
                if (app_config.contains("processes") && app_config["processes"].is_array()) {
                    for (const auto& process : app_config["processes"]) {
                        if (process.is_string()) {
                            output_string += process.get<std::string>() + " ";
                        }
                    }
                }
            }
        }
    }

    // Remove trailing space if any
    if (!output_string.empty() && output_string.back() == ' ') {
        output_string.pop_back();
    }

    std::cout << output_string << std::endl;

    return 0;
}