#include "CommandLine.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

CommandLine::CommandLine(int argc, char* argv[], int firstOption)
    : words(argv + std::min(firstOption, argc), argv + argc), position(0)
{
}

bool
CommandLine::next()
{
    if (position >= words.size())
        return false;
    current = words[position++];
    return true;
}

bool
CommandLine::hasValue() const
{
    return position < words.size() && words[position].rfind("--", 0) != 0;
}

std::string
CommandLine::text()
{
    if (!hasValue())
        throw std::invalid_argument("Missing value for " + current);
    return words[position++];
}

double
CommandLine::number()
{
    return toNumber(text());
}

std::vector<double>
CommandLine::numbers()
{
    std::vector<double> values;
    for (const auto& part : list())
        values.push_back(toNumber(part));
    return values;
}

double
CommandLine::toNumber(const std::string& value) const
{
    try
    {
        size_t used = 0;
        double result = std::stod(value, &used);
        if (used == value.size())
            return result;
    }
    catch (const std::exception&)
    {
    }
    badValue(value);
}

int
CommandLine::whole()
{
    std::string value = text();
    try
    {
        size_t used = 0;
        int result = std::stoi(value, &used);
        if (used == value.size())
            return result;
    }
    catch (const std::exception&)
    {
    }
    badValue(value);
}

std::vector<std::string>
CommandLine::list()
{
    return split(text());
}

void
CommandLine::rejectOption() const
{
    throw std::invalid_argument("Unknown option: " + current);
}

void
CommandLine::badValue(const std::string& value) const
{
    throw std::invalid_argument("Bad value for " + current + ": " + value);
}

std::vector<std::string>
CommandLine::split(const std::string& text)
{
    std::vector<std::string> parts;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ','))
        if (!part.empty())
            parts.push_back(part);
    return parts;
}
