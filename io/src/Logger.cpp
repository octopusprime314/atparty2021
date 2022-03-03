/*
 * Copyright (c) 2016, 2017
 * The University of Rhode Island
 * All Rights Reserved
 *
 * This code is part of the Pattern Finder.
 *
 * Permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any
 * purpose, so long as the copyright notice above, this grant of
 * permission, and the disclaimer below appear in all copies made; and
 * so long as the name of The University of Rhode Island is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION FROM THE
 * UNIVERSITY OF RHODE ISLAND AS TO ITS FITNESS FOR ANY PURPOSE, AND
 * WITHOUT WARRANTY BY THE UNIVERSITY OF RHODE ISLAND OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE. THE UNIVERSITY OF RHODE ISLAND SHALL NOT BE
 * LIABLE FOR ANY DAMAGES, INCLUDING DIRECT, SPECIAL, INDIRECT,
 * INCIDENTAL, OR CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.
 *
 * Author: Peter J. Morley
 *
 */

#include "Logger.h"
#include <Windows.h>
#include <chrono>
#include <ctime>
#include <iomanip>

std::ofstream* Logger::outputFile = nullptr;

std::mutex* Logger::logMutex  = new std::mutex();
int         Logger::verbosity = 1;
LOG_LEVEL   Logger::logLevel  = LOG_LEVEL::TRACE;

std::string Logger::getPID()
{
    // Return the program PID to generate unique log files
    return std::to_string(GetCurrentProcessId());
}

// Close log and save time
void Logger::closeLog()
{
    logMutex->lock();
    //outputFile->close();
    logMutex->unlock();

    delete outputFile;
    delete logMutex;
}

void Logger::setLogLevel(std::string log_level)
{
    if (log_level == "fatal")
    {
        Logger::logLevel = LOG_LEVEL::FATAL;
    }
    else if (log_level == "err")
    {
        Logger::logLevel = LOG_LEVEL::ERR;
    }
    else if (log_level == "warn")
    {
        Logger::logLevel = LOG_LEVEL::WARN;
    }
    else if (log_level == "info")
    {
        Logger::logLevel = LOG_LEVEL::INFO;
    }
    else if (log_level == "debug")
    {
        Logger::logLevel = LOG_LEVEL::DEBUGGING;
    }
    else if (log_level == "trace")
    {
        Logger::logLevel = LOG_LEVEL::TRACE;
    }
}

void Logger::dumpLog(const char* file, const char* func, int line, LOG_LEVEL level,
                     const std::string& buffer)
{

    if (cmdEnabled)
    {
        std::cout << buffer;
    }

    std::chrono::time_point<std::chrono::system_clock> nowTimePoint =
        std::chrono::system_clock::now();
    auto duration = nowTimePoint.time_since_epoch();
    auto millis   = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    (*outputFile) << "[" << std::setw(9) << std::put_time(std::localtime(&now), "%T:")
                  << std::setw(3) << millis << "] ";

    switch (level)
    {
        case LOG_LEVEL::FATAL:
            (*outputFile) << "FATAL ";
            break;
        case LOG_LEVEL::ERR:
            (*outputFile) << "ERROR ";
            break;
        case LOG_LEVEL::WARN:
            (*outputFile) << "WARN ";
            break;
        case LOG_LEVEL::INFO:
            (*outputFile) << "INFO: ";
            break;
        case LOG_LEVEL::DEBUGGING:
            (*outputFile) << "DEBUG ";
            break;
        case LOG_LEVEL::TRACE:
            (*outputFile) << "TRACE ";
            break;
    }
    (*outputFile) << file << ":" << func << ":" << line << " ";
    (*outputFile) << buffer.c_str();
}