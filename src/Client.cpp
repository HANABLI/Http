/**
 * @file Client.cpp
 *
 * This module is an implementation of the
 * Http::Client class.
 *
 * © 2024 by Hatem Nabli
 */

#include <Http/Client.hpp>
#include <StringUtils/StringUtils.hpp>
#include <sstream>
#include <string>

namespace
{
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
    bool ParseSize(const std::string& stringSize, size_t& number) {
        number = 0;
        for (auto c : stringSize)
        {
            if ((c < '0') || (c > '9'))
            { return false; }
            auto previousNumber = number;
            number *= 10;
            number += (uint16_t)(c - '0');
            if ((number / 10) != previousNumber)
            { return false; }
        }
        return true;
    }

    /**
     * This method parses the status, statusCode, and protocol identifier
     * from the given response line.
     *
     * @param[in] response
     *      This is the response in which to store the parsed status and
     *      statusCode.
     *
     * @param[in] responseLine
     *      This is the raw response line string to parse.
     *
     * @return
     *      Returns an indication of whether or not the response line
     *      was successfuly parsed
     */
    bool ParseResponseLine(std::shared_ptr<Http::Client::Response> response,
                           const std::string& responseLine) {
        // Parse the method
        const auto protocolDelimiter = responseLine.find(' ');
        if (protocolDelimiter == std::string::npos)
        { return false; }

        const auto protocol = responseLine.substr(0, protocolDelimiter);

        // Parse the target URI.
        const auto statusCodeDelimiter = responseLine.find(' ', protocolDelimiter + 1);

        if (statusCodeDelimiter == std::string::npos)
        { return false; }

        intmax_t statusCodeAsInt;
        if (StringUtils::ToInteger(responseLine.substr(protocolDelimiter + 1,
                                                       statusCodeDelimiter - protocolDelimiter - 1),
                                   statusCodeAsInt) != StringUtils::ToIntegerResult::Success)
        { return false; }

        if (statusCodeAsInt > 999)
        {
            return false;
        } else
        { response->statusCode = (unsigned int)statusCodeAsInt; }

        response->status = responseLine.substr(statusCodeDelimiter + 1);

        return (protocol == "HTTP/1.1");
    }

}  // namespace

namespace Http
{
    /**
     * This contains the private properties of a Client instance
     */
    struct Client::Impl
    {
    };

    Client::~Client() = default;

    Client::Client() : impl_(new Impl) {}

    auto Client::ParseResponse(const std::string& rawResponse) -> std::shared_ptr<Response> {
        size_t messageEnd;
        return ParseResponse(rawResponse, messageEnd);
    }

    auto Client::ParseResponse(const std::string& rawResponse, size_t& messageEnd)
        -> std::shared_ptr<Response> {
        const auto response = std::make_shared<Response>();
        // First, extarct the response line.
        const auto responseLineEnd = rawResponse.find(CRLF);
        if (responseLineEnd == std::string::npos)
        { return nullptr; }
        const auto responseLine = rawResponse.substr(0, responseLineEnd);
        if (!ParseResponseLine(response, responseLine))
        { return nullptr; }
        // Second, parse the message headers and identify where the body begins.
        size_t bodyOffset;
        size_t headerOffset = responseLineEnd + CRLF.length();
        switch (response->headers.ParseRawMessage(rawResponse.substr(headerOffset), bodyOffset))
        {
        case MessageHeaders::MessageHeaders::State::Complete: {
            if (!response->headers.IsValid())
            { return nullptr; }
        }
        break;
        case MessageHeaders::MessageHeaders::State::Incomplete:
            return nullptr;
        case MessageHeaders::MessageHeaders::State::Error:
            return nullptr;
        default:
            return nullptr;
        }
        // Check for "Content-Length" header, if present, use it to
        // determine how many characters should be in the body.
        bodyOffset += headerOffset;
        const auto maxContentLength = rawResponse.length() - bodyOffset;
        // If it containt "Content-Length" header, we carefully carve exactly
        // that number of cahracters out (and bail if we don't have anough)
        if (response->headers.HasHeader("Content-Length"))
        {
            size_t contentLength;
            if (!ParseSize(response->headers.GetHeaderValue("Content-Length"), contentLength))
            { return nullptr; }
            if (contentLength > maxContentLength)
            {
                return nullptr;
            } else
            {
                response->body = rawResponse.substr(bodyOffset, contentLength);
                messageEnd = bodyOffset + contentLength;
            }
        } else
        {
            // Finally, extract the body
            response->body.clear();
            messageEnd = bodyOffset;
        }

        return response;
    }

    std::string Client::Response::GenerateToString() const {
        std::ostringstream builder;
        builder << "HTTP/1.1 " << statusCode << ' ' << status << "\r\n";
        builder << headers.GenerateRawHeaders();
        builder << body;
        return builder.str();
    }

}  // namespace Http