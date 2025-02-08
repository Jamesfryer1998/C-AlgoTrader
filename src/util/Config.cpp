#include "Config.hpp"
#include <fstream>

#define JSON_CONFIG_PATH "./algo_trader.json";

using json = nlohmann::json;

Config::Config() 
{
}

Config::~Config() 
{
}

void
Config::loadJson(const std::string& path) 
{
    std::ifstream f(path);
    json config = json::parse(f);
    configData = config;
}