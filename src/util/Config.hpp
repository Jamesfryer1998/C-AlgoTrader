#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Config
{
    public:
        Config();
        ~Config();
        void loadJson(const std::string& filePath);

    private:
        json configData;
};