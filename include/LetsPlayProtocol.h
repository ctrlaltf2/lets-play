/**
 * @file LetsPlayProtocol.h
 *
 * @author ctrlaltf2
 *
 *  @section DESCRIPTION
 *  Class that defines the encode/decode protocol for the server
 */

class LetsPlayProtocol;

#pragma once
#include <sstream>
#include <string>
#include <vector>

/**
 * @class LetsPlayProtocol
 *
 * All static class that contains the encode/decode functions for the Let's Play
 * protocol. Messages consist of a list of strings stitched together by this protocol.
 * Each item is preceded by its string length, followed by a '.', followed by the
 * string. If more items succeed the 'chunk', a comma is next. Otherwise, a semicolon
 * ends the message. An example would be <i>7.connect,4.emu1;</i>
 */
class LetsPlayProtocol {
  public:
    /**
     * Vector-based function for encoding messages
     *
     * @param chunks The 'chunks' of information to stich together.
     *
     * @return The encoded string
     */
    static std::string encode(const std::vector<std::string>& chunks);

    /**
     * Variadic function for encoding messages.
     *
     * @note Wraps around encodeImpl.
     * @note Data types that go into this function must have << overloaded
     * for output streams.
     *
     * @return The encoded string
     *
     *
     */
    template<typename Head, typename... Tail>
    static std::string encode(Head h, Tail... t) {
        std::string encoded;
        encodeImpl(encoded, h, t...);
        encoded.back() = ';';
        return encoded;
    }

    /**
     * Base case for the encode variadic implementation
     *
     * @param encoded The running encoded string
     * @param item The item being looked at
     *
     * @note Data types that go into this function must have << overloaded
     * for output streams.
     */
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

    /**
     * Recursive part of the variadic encode implementation
     *
     * @param encoded The running encoded string
     * @param h The current item being looked at
     * @param t The rest of the items being looked at
     *
     * @note Data types that go into this function must have << overloaded
     * for output streams.
     */
    template<typename Head, typename... Tail>
    static void encodeImpl(std::string& encoded, Head h, Tail... t) {
        encodeImpl(encoded, h);
        encodeImpl(encoded, t...);
    }

    /**
     * Helper function for decoding messages
     *
     * @param input The encoded string to decode into multiple strings
     *
     * @return A list containing the decoded values, or empty if an invalid string.
     */
    static std::vector<std::string> decode(const std::string& input);
};
