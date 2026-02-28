#include "log.hpp"
Logger &Logger::getInstance()
{
    static Logger instance;
    return instance;
}