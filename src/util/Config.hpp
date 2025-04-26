#pragma once
#include <iostream>
#include <nlohmann/json.hpp>

#define JSON_CONFIG_NAME "/src/config/config.json"

using json = nlohmann::json;
using namespace std;

// Check how we set up JSON CMAKE at work

class Config
{
    public:
        Config();
        ~Config();
        
        // Load JSON from a file path (can be absolute or relative)
        void loadJson(const std::string& filePath);
        
        // Load the default config file
        json loadConfig();
        
        // Get the loaded JSON data
        json getJson(){ return configData; };
        
        // Get the project root directory
        string getProjectRoot();
        
        // Form the path to the default config file
        string formConfigPath();
        
        // Convert a path relative to the project root into an absolute path
        // Example: "tests/data/file.json" -> "/full/path/to/projectroot/tests/data/file.json"
        string getAbsolutePath(const std::string& relativePath);
        
        // Get a path relative to the tests directory
        // Example: "test_data/config.json" -> "/full/path/to/projectroot/tests/test_data/config.json"
        string getTestPath(const std::string& relativeTestPath);
        
        // Debug method to print information about file paths
        // Useful for troubleshooting when tests can't find files
        void debugPaths(const std::string& testPath);

    private:
        json configData;
        bool loaded = false;
};