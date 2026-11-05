#include "DotEnv.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace
{
    const char* const SPACES = " \t\r\n";
    const char COMMENT = '#';
    const std::string EXPORT_PREFIX = "export ";   // "export KEY=value" works too
}

static std::string trim(const std::string& text)
{
    size_t start = text.find_first_not_of(SPACES);
    if (start == std::string::npos)
        return "";
    size_t end = text.find_last_not_of(SPACES);
    return text.substr(start, end - start + 1);
}

bool
DotEnv::parseLine(const std::string& rawLine, std::string& key, std::string& value)
{
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == COMMENT)
        return false;

    if (line.rfind(EXPORT_PREFIX, 0) == 0)
        line = trim(line.substr(EXPORT_PREFIX.size()));

    size_t equals = line.find('=');
    if (equals == std::string::npos || equals == 0)
        return false;

    key = trim(line.substr(0, equals));
    value = trim(line.substr(equals + 1));

    // Strip matching surrounding quotes: "value" or 'value'
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
        value = value.substr(1, value.size() - 2);

    return !key.empty();
}

int
DotEnv::load(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file)
        return 0;

    int loaded = 0;
    std::string line, key, value;
    while (std::getline(file, line))
    {
        if (!parseLine(line, key, value))
            continue;

        // overwrite = 0: a variable already exported in the shell takes priority
        if (setenv(key.c_str(), value.c_str(), 0) == 0)
            loaded++;
    }

    std::cout << "Loaded " << loaded << " setting(s) from " << filePath << std::endl;
    return loaded;
}
