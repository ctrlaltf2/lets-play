/**
 * @file Logging.hpp
 *
 * @author ctrlaltf2
 *
 */
#pragma once
#include <chrono>
#include <cstdarg>
#include <vector>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <libretro.h>

class Logger {
    std::mutex m;
  public:
    template<typename T, typename... Others>
    void log(const T value, Others... others) {
        auto message = Logger::timestamp() + Logger::stringify(value, others...) + '\n';
        std::unique_lock<std::mutex> lk(m);
        std::clog << message;
    }

    template<typename T, typename... Others>
    void out(const T value, Others... others) {
        auto message = Logger::timestamp() + Logger::stringify(value, others...) + '\n';
        std::unique_lock<std::mutex> lk(m);
        std::cout << message;
    }

    template<typename T, typename... Others>
    void err(const T value, Others... others) {
        auto message = Logger::timestamp() + Logger::stringify(value, others...) + '\n';
        std::unique_lock<std::mutex> lk(m);
        std::cerr << message;
    }

  private:
    static std::string timestamp() {
        std::chrono::time_point<std::chrono::system_clock> p = std::chrono::system_clock::now();

        std::time_t t = std::chrono::system_clock::to_time_t(p);
        std::string ts = std::string(std::ctime(&t));

        ts.back() = ']';
        ts.push_back('\t');
        ts.insert(ts.begin(), '[');

        return ts;
    }

    template<typename Head, typename... Tail>
    static std::string stringify(Head h, Tail... t) {
        std::string s;
        stringifyImpl(s, h, t...);
        return s;
    }

    template<typename Head, typename...Tail>
    static void stringifyImpl(std::string& strung, Head h, Tail... t) {
        stringifyImpl(strung, h);
        stringifyImpl(strung, t...);
    }

    template<typename T>
    static void stringifyImpl(std::string& strung, T item) {
        // Convert item to a string
        std::ostringstream toString;
        toString << item;
        const std::string itemAsString = toString.str();

        strung += itemAsString;
    }
};