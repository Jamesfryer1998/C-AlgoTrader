#include "Config.hpp"
#include <fstream>

Config::Config() 
{
}

Config::~Config() 
{
}

json
Config::loadConfig()
{
    if(!loaded)
    {
        loadJson(formConfigPath());
        std::cout << "JSON Data: " << configData.dump(4) << std::endl;
        return configData;
    }
    else
    {
        return configData;
    }
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
    loaded = true;
}

string
Config::formConfigPath()
{
    string projectRoot = getProjectRoot();
    string configPath = projectRoot + JSON_CONFIG_NAME;
    return configPath;
}

string
Config::getProjectRoot()
{
    std::filesystem::path exePath = std::filesystem::current_path();
    return exePath.string();
}