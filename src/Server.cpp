/**
 * @file Server.cpp
 * 
 * This module is an implementation of the
 * Http::Server class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <Http/Server.hpp>

namespace {
    /**
     * This is the character sequence corresponding to a carriage return (CR)
     * followed by a line feed (LF), which officially delimits each
     * line of an HTTP request.
     */
    const std::string CRLF("\r\n");

    /**
     * @return 
     *      An indication of whether or not the number was parsed
     *      successfully is returned
     */
    bool ParseSize(
        const std::string& stringSize,
        size_t& number 
    ) {
        number = 0;
        for ( auto c : stringSize ) {
            if (
                (c < '0')
                || (c > '9')
            ) { 
                return false;
            }
            auto previousNumber = number;
            number *= 10;
            number += (uint16_t)(c - '0');
            if (
                (number / 10) != previousNumber
            ) {
                return false;
            }
        }
        return true;
    }

    /**
     * This method parses the method, target URI, and protocol identifier
     * from the given request line.
     * 
     * @param[in] request
     *      This is the request in which to store the parsed method and 
     *      target URI.
     * 
     * @param[in] requestLine
     *      This is the raw request line string to parse.
     * 
     * @return
     *      Returns an indication of whether or not the request line
     *      was successfuly parsed 
     */
    bool ParseRequestLine(std::shared_ptr< Http::Server::Request > request, const std::string& requestLine) {

        //Parse the method
        const auto methodDelimiter = requestLine.find(' ');
        if (methodDelimiter == std::string::npos) {
            return false;
        }

        request->method = requestLine.substr(0, methodDelimiter);

        // Parse the target URI.
        const auto targetDelimiter = requestLine.find(' ', methodDelimiter + 1);
        if (!request->target.ParseFromString(
                requestLine.substr(
                    methodDelimiter + 1, 
                    targetDelimiter - methodDelimiter - 1 
                )
            )
        ){
            return false;
        }

        // Parse the protocol.
        const auto protocol = requestLine.substr(targetDelimiter + 1);

        return (protocol == "HTTP/1.1");
    }

}

namespace Http {

    /**
     * This contains the private properties of a Server instance
     */

    struct Server::Impl
    {
        
    };

    Server::~Server() = default;

    Server::Server()
         : impl_(new Impl) {

    }

    auto Server::ParseRequest(const std::string& rawRequest)-> std::shared_ptr< Request > {
        size_t messageEnd;
        return ParseRequest(rawRequest, messageEnd);
    }

    auto Server::ParseRequest(const std::string& rawRequest, size_t& messageEnd)-> std::shared_ptr< Request > {
        const auto request = std::make_shared< Request >();
        // First, extarct the request line.
        const auto requestLineEnd = rawRequest.find(CRLF);
        if (requestLineEnd == std::string::npos) {
            return nullptr;
        }
        const auto requestLine = rawRequest.substr(0, requestLineEnd);
        if (!ParseRequestLine(request, requestLine)) {
            return nullptr;
        }
        //Second, parse the message headers and identify where the body begins.
        size_t bodyOffset;
        size_t headerOffset = requestLineEnd + CRLF.length();
        if (
            !request->headers
                .ParseRawMessage(
                    rawRequest.substr(headerOffset),
                    bodyOffset
                )
        ) {
            return nullptr;
        }
        // Check for "Content-Length" header, if present, use it to
        // determine how many characters should be in the body.
        bodyOffset += headerOffset;
        const auto maxContentLength = rawRequest.length() - bodyOffset;
        // If it containt "Content-Length" header, we carefully carve exactly
        // that number of cahracters out (and bail if we don't have anough) 
        if (request->headers.HasHeader("Content-Length")) {
            size_t contentLength;
            if (!ParseSize(request->headers.GetHeaderValue("Content-Length"), contentLength)) {
                return nullptr;
            }
            if (contentLength > maxContentLength) {
                return nullptr;
            } else {
                request->body = rawRequest.substr(bodyOffset, contentLength);
                messageEnd = bodyOffset + contentLength;
            }
        } else {
            //Finally, extract the body
            request->body = rawRequest.substr(bodyOffset);
            messageEnd = rawRequest.length();
        }
        

        return request;
    }
    
}