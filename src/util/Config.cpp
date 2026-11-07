#include "Config.hpp"
#include "Log.hpp"
#include <fstream>
#include <map>
#include <vector>

namespace
{
    // Folders the apps and tests are run from, one below the project root
    const std::vector<std::string> FOLDERS_BELOW_ROOT = {"build", "tests", "bin"};

    const std::string TESTS_FOLDER = "tests";
    const std::string TEST_DATA_FOLDER = "test_data";   // where the tests' build copies their data
}

Config::Config() 
{
}

Config::~Config() 
{
}

json
Config::loadConfig()
{
    if (loaded)
        return configData;

    json merged = json::object();
    std::map<std::string, std::string> fileOfKey;
    for (const auto& configFile : ConfigFiles::APP_CONFIG)
    {
        loadJson(formConfigPath(configFile));
        for (const auto& [key, value] : configData.items())
        {
            if (fileOfKey.count(key))
                throw std::runtime_error("\"" + key + "\" is in both " + fileOfKey[key] + " and " + configFile);
            fileOfKey[key] = configFile;
            merged[key] = value;
        }
    }
    configData = merged;
    Log::out() << "JSON Data: " << configData.dump(4) << std::endl;
    return configData;
}

void 
Config::loadJson(const std::string& path) 
{
    std::ifstream file(path);
    
    if (!file) {
        throw std::runtime_error("Failed to open JSON file: " + path);
    }

    Log::out() << "Loading JSON from: " << path << std::endl;

    json j;
    file >> j;  
    configData = j;
    loaded = true;
}

string
Config::formConfigPath(const string& configFile)
{
    return getProjectRoot() + "/" + configFile;
}

string
Config::getProjectRoot()
{
    // Find the project root by getting the current working directory
    // In a real-world scenario, this might need to be more sophisticated to correctly
    // identify the project root regardless of where the executable is run from
    std::filesystem::path exePath = std::filesystem::current_path();
    
    // If we're in a subdirectory like build/, tests/, etc., go up to find the project root
    // This is a simple approach; for a more robust solution, you might want to look for
    // specific files that indicate the project root (e.g., CMakeLists.txt, .git/, etc.)
    for (const auto& folder : FOLDERS_BELOW_ROOT) {
        if (exePath.filename() == folder) {
            return exePath.parent_path().string();
        }
    }
    
    return exePath.string();
}

string
Config::getTestPath(const std::string& relativeTestPath)
{
    // First, check for the file in the current directory
    // This is for when tests copy test data to the build directory
    std::filesystem::path currentPath = std::filesystem::current_path();
    std::filesystem::path localPath = currentPath / relativeTestPath;
    
    if (std::filesystem::exists(localPath)) {
        return localPath.string();
    }
    
    // Check for file in test_data subdirectory (common pattern)
    std::filesystem::path localTestDataPath = currentPath / TEST_DATA_FOLDER / relativeTestPath;
    if (std::filesystem::exists(localTestDataPath)) {
        return localTestDataPath.string();
    }
    
    // Next, try a direct path from the current directory to the tests folder
    // This handles the case when tests are run from the build directory
    std::filesystem::path directPath = currentPath / ".." / ".." / TESTS_FOLDER / relativeTestPath;
    if (std::filesystem::exists(directPath)) {
        return directPath.string();
    }
    
    // Try from the project root (for when running outside the build directory)
    std::filesystem::path projectRoot = getProjectRoot();
    std::filesystem::path rootPath = projectRoot / TESTS_FOLDER / relativeTestPath;
    if (std::filesystem::exists(rootPath)) {
        return rootPath.string();
    }
    
    // Try various relative paths that might work in different build configurations
    const std::vector<std::string> relativeOptions = {
        "../" + TESTS_FOLDER,       // If in build dir
        TESTS_FOLDER,               // If in project root
        "../../../" + TESTS_FOLDER, // If deeply nested in build
        "../../..",                 // Try project root
        "../../../../" + TESTS_FOLDER  // More nesting
    };
    
    for (const auto& option : relativeOptions) {
        std::filesystem::path optionPath = currentPath / option / relativeTestPath;
        if (std::filesystem::exists(optionPath)) {
            return optionPath.string();
        }
    }
    
    // If we can't find it, log a warning and fallback to a relative path
    // which will be used for error reporting
    std::cout << "WARNING: Could not find test file: " << relativeTestPath << std::endl;
    
    // Use our debug method to print extra info about paths
    debugPaths(relativeTestPath);
    
    // Return a path relative to the current directory as a last resort
    return (currentPath / TEST_DATA_FOLDER / relativeTestPath).string();
}

void
Config::debugPaths(const std::string& testPath)
{
    std::filesystem::path currentPath = std::filesystem::current_path();
    std::filesystem::path projectRoot = getProjectRoot();
    
    std::cout << "Debug path information:" << std::endl;
    std::cout << "  Current working directory: " << currentPath.string() << std::endl;
    std::cout << "  Project root directory:    " << projectRoot.string() << std::endl;
    std::cout << "  Requested test path:       " << testPath << std::endl;
    
    // Check if various combinations exist
    std::vector<std::filesystem::path> possiblePaths = {
        currentPath / testPath,
        currentPath / TEST_DATA_FOLDER / testPath,
        projectRoot / TESTS_FOLDER / testPath,
        currentPath / ".." / ".." / TESTS_FOLDER / testPath,
        currentPath / ".." / TESTS_FOLDER / testPath,
        currentPath / ".." / ".." / ".." / TESTS_FOLDER / testPath
    };
    
    std::cout << "  Checking possible paths:" << std::endl;
    for (const auto& path : possiblePaths) {
        std::cout << "    " << path.string() << ": " 
                  << (std::filesystem::exists(path) ? "EXISTS" : "NOT FOUND") << std::endl;
    }
    
    // Check if the test_data directory exists
    std::filesystem::path testDataDir = currentPath / TEST_DATA_FOLDER;
    std::cout << "  test_data directory: " << testDataDir.string() << ": "
              << (std::filesystem::exists(testDataDir) ? "EXISTS" : "NOT FOUND") << std::endl;
    
    // If it exists, list its contents
    if (std::filesystem::exists(testDataDir)) {
        std::cout << "  Contents of test_data directory:" << std::endl;
        for (const auto& entry : std::filesystem::directory_iterator(testDataDir)) {
            std::cout << "    " << entry.path().filename().string() << std::endl;
        }
    }
}