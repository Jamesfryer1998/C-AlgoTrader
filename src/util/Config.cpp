#include "Config.hpp"
#include <fstream>

Config::Config() 
{
    loadJson(JSON_CONFIG_PATH);
}

Config::~Config() 
{
}

void 
Config::loadJson(const std::string& path) 
{
    std::ifstream file(path);
    
    if (!file) {
        throw std::runtime_error("Failed to open JSON file: " + path);
    }

    std::cout << "Loading JSON from: " << path << std::endl;

    json j;
    file >> j;  
    configData = j;
}