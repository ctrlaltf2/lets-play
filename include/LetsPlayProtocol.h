class LetsPlayProtocol;

#pragma once
#include <sstream>
#include <string>
#include <vector>


class LetsPlayProtocol {
  public:
    /*
     * Vector function for encoding messages
     */
    static std::string encode(const std::vector<std::string>& chunks);

    /*
     * Variadic function for encoding messages
     */
    template<typename Head, typename... Tail>
    static std::string encode(Head h, Tail... t) {
        std::string encoded;
        encodeImpl(encoded, h, t...);
        encoded.back() = ';';
        return encoded;
    }

    /*
     * Variadic implementation of the encoding function
     */

    // Base
    template<typename T>
    static void encodeImpl(std::string& encoded, const T& item) {
        // Convert item to a string
        std::ostringstream toString;
        toString << item;
        const std::string itemAsString = toString.str();

        // Append the item string size to the output
        encoded += std::to_string(itemAsString.size());
        encoded += '.';
        // Append the item to the output
        encoded += itemAsString;
        encoded += ',';
    }

    // Recursive impl
    template<typename Head, typename... Tail>
    static void encodeImpl(std::string& encoded, Head h, Tail... t) {
        encodeImpl(encoded, h);
        encodeImpl(encoded, t...);
    }

    /*
     * Helper function for decoding messages
     * @param input The encoded string to decode into multiple strings
     */
    static std::vector<std::string> decode(const std::string& input);

};
