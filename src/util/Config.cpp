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
    // Find the project root by getting the current working directory
    // In a real-world scenario, this might need to be more sophisticated to correctly
    // identify the project root regardless of where the executable is run from
    std::filesystem::path exePath = std::filesystem::current_path();
    
    // If we're in a subdirectory like build/, tests/, etc., go up to find the project root
    // This is a simple approach; for a more robust solution, you might want to look for
    // specific files that indicate the project root (e.g., CMakeLists.txt, .git/, etc.)
    if (exePath.filename() == "build" || exePath.filename() == "tests" || exePath.filename() == "bin") {
        return exePath.parent_path().string();
    }
    
    return exePath.string();
}

string
Config::getAbsolutePath(const std::string& relativePath)
{
    // If the path is already absolute, return it as is
    if (!relativePath.empty() && relativePath[0] == '/') {
        return relativePath;
    }
    
    // Otherwise, combine it with the project root
    std::filesystem::path projectRoot = getProjectRoot();
    std::filesystem::path fullPath = projectRoot / relativePath;
    
    return fullPath.string();
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
    std::filesystem::path localTestDataPath = currentPath / "test_data" / relativeTestPath;
    if (std::filesystem::exists(localTestDataPath)) {
        return localTestDataPath.string();
    }
    
    // Next, try a direct path from the current directory to the tests folder
    // This handles the case when tests are run from the build directory
    std::filesystem::path directPath = currentPath / "../../tests" / relativeTestPath;
    if (std::filesystem::exists(directPath)) {
        return directPath.string();
    }
    
    // Try from the project root (for when running outside the build directory)
    std::filesystem::path projectRoot = getProjectRoot();
    std::filesystem::path rootPath = projectRoot / "tests" / relativeTestPath;
    if (std::filesystem::exists(rootPath)) {
        return rootPath.string();
    }
    
    // Try various relative paths that might work in different build configurations
    const std::vector<std::string> relativeOptions = {
        "../tests",           // If in build dir
        "tests",              // If in project root
        "../../../tests",     // If deeply nested in build
        "../../..",           // Try project root
        "../../../../tests"   // More nesting
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
    return (currentPath / "test_data" / relativeTestPath).string();
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
        currentPath / "test_data" / testPath,
        projectRoot / "tests" / testPath,
        currentPath / "../../tests" / testPath,
        currentPath / "../tests" / testPath,
        currentPath / "../../../tests" / testPath
    };
    
    std::cout << "  Checking possible paths:" << std::endl;
    for (const auto& path : possiblePaths) {
        std::cout << "    " << path.string() << ": " 
                  << (std::filesystem::exists(path) ? "EXISTS" : "NOT FOUND") << std::endl;
    }
    
    // Check if the test_data directory exists
    std::filesystem::path testDataDir = currentPath / "test_data";
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