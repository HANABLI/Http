/**
 * @file Server.cpp
 * 
 * This module is an implementation of the
 * Http::Server class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <set>
#include <string>
#include <Http/Server.hpp>

namespace {
    /**
     * This is the character sequence corresponding to a carriage return (CR)
     * followed by a line feed (LF), which officially delimits each
     * line of an HTTP request.
     */
    const std::string CRLF("\r\n");

    /**
     * This is the maximum allowed request body size.
     */
    constexpr size_t MAX_CONTENT_LENGTH = 10000000;

    /**
     * This is the maximum allowed request header line size.
     */
    constexpr size_t HEADER_LINE_LIMIT = 1000;

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
        if (request->method.empty()) {
            return false;
        }

        // Parse the target URI.
        const auto targetDelimiter = requestLine.find(' ', methodDelimiter + 1);
        const auto targetLength = targetDelimiter - methodDelimiter -1;
        if (targetLength == 0 ) {
            return false;
        }
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

    /**
     *  This structure holds onto all state information the server has
     *  about a single connection from a client.
     */
    struct ConnectionState {

        /**
         * This is the transport interface of the connection.
         */
        std::shared_ptr< Http::Connection > connection;

        /**
         * This buffer is used to conctatenate fragmented HTTP requests
         * received from the client.
         */
        std::string concatenateBuffer;

        // Methods
        /**
         * This method appends the given data to the end of the concatenate
         * buffer, and then attempts to parse a request out of it.
         * 
         * @return 
         *      The request parsed from the concatenate buffer is returned.
         * 
         * @retval nullptr
         *      This is returned if no request could be parsed from the 
         *      concatinate buffer
         */
        std::shared_ptr< Http::Server::Request > TryRequestAssembly() {
            size_t messageEnd;
            const auto request = Http::Server::ParseRequest(
                concatenateBuffer,
                messageEnd
            );
            if (request == nullptr) {
                return nullptr;
            }
            concatenateBuffer.erase(
                concatenateBuffer.begin(),
                concatenateBuffer.begin() + messageEnd
            );

            return request;
        }

    };

}

namespace Http {

    /**
     * This contains the private properties of a Server instance
     */

    struct Server::Impl
    {
        /**
         * This is the transport layer currently bound.
         */
        std::shared_ptr< ServerTransportLayer > transport;
        
        /**
         * These are the currently established connections.
         */
        std::set< std::shared_ptr< ConnectionState > > establishedConnections;

        /**
         * This method is called when new data is received from a connection
         * 
         * @param[in] connectionState
         *      This is the connection state of the connection from which 
         *      data was received.
         * 
         * @param[in] data
         *      This is a copy of the data that was received from 
         *      the connection.
         */
        void DataReceived(
            std::shared_ptr< ConnectionState > connectionState,
            std::vector< uint8_t > data
        ) {
            connectionState->concatenateBuffer += std::string(data.begin(), data.end());
            for (;;) {
                const auto request = connectionState->TryRequestAssembly();
                if (request == nullptr) {
                    break;
                }
                std::string response;
                if (request->validity == Request::Validity::Valid) {
                    const std::string cannedResponse = (
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: 13\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "BadRequest.\r\n"
                    );
                    response = cannedResponse;
                } else {
                    const std::string cannedResponse = (
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Length: 13\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "BadRequest.\r\n"
                    );
                    response = cannedResponse;
                }

                connectionState->connection->SendData(
                    std::vector< uint8_t >(
                        response.begin(),
                        response.end()
                    )
                );
                if (request->validity != Request::Validity::Valid) {
                    if(request->validity == Request::Validity::InvalidUnrecoverable) {
                        connectionState->connection->Break(true);
                    }
                    break;
                }
            }
        }

        /**
         * This method is called when a new connection has been
         * established for the server.
         * 
         * @param[in] connection
         *      This is the new connection has been established for the server.
         */
        void NewConnection(std::shared_ptr< Connection > connection) {
            const auto connectionState = std::make_shared< ConnectionState >();
            connectionState->connection = connection;
            (void)establishedConnections.insert(connectionState);
            std::weak_ptr< ConnectionState > connectionStateWeak(connectionState);
            connectionState->connection->SetDataReceivedDelegate(
                [this, connectionStateWeak](std::vector< uint8_t > data){
                    const auto connectionState = connectionStateWeak.lock();
                    if (connectionState == nullptr) {
                        return;
                    }
                    DataReceived(
                        connectionState,
                        data
                    );
                }
            );
        }
    };

    Server::~Server() {
        Demobilize();
    };

    Server::Server()
         : impl_(new Impl) {

    }

    auto Server::ParseRequest(const std::string& rawRequest)-> std::shared_ptr< Request > {
        size_t messageEnd;
        return ParseRequest(rawRequest, messageEnd);
    }

    auto Server::ParseRequest(const std::string& rawRequest, size_t& messageEnd)-> std::shared_ptr< Request > {
        const auto request = std::make_shared< Request >();
        messageEnd = 0;
        // First, extarct the request line.
        const auto requestLineEnd = rawRequest.find(CRLF);
        if (requestLineEnd == std::string::npos) {
            return nullptr;
        }
        const auto requestLine = rawRequest.substr(0, requestLineEnd);
        if (!ParseRequestLine(request, requestLine)) {
            request->validity = Request::Validity::InvalidRecoverable;
        }
        //Second, parse the message headers and identify where the body begins.
        size_t bodyOffset = 0;
        const auto headerOffset = requestLineEnd + CRLF.length();
        request->headers.SetLineLimit(HEADER_LINE_LIMIT);
        switch (
            request->headers
                .ParseRawMessage(
                    rawRequest.substr(headerOffset),
                    bodyOffset
                )
        ) {
            case MessageHeaders::MessageHeaders::Validity::Valid: break;

            case MessageHeaders::MessageHeaders::Validity::ValidIncomplete : return nullptr;
            
            case MessageHeaders::MessageHeaders::Validity::InvalidRecoverable: 
            {
                request->validity = Request::Validity::InvalidRecoverable;
            } break;
            case MessageHeaders::MessageHeaders::Validity::InvalidUnrecoverable: 
            default: 
            {
                request->validity = Request::Validity::InvalidUnrecoverable;
                return request;
            }
        }
        // Check for "Content-Length" header, if present, use it to
        // determine how many characters should be in the body.
        bodyOffset += headerOffset;
        const auto bodyAvailableSize = rawRequest.length() - bodyOffset;
        // If it containt "Content-Length" header, we carefully carve exactly
        // that number of cahracters out (and bail if we don't have anough) 
        if (request->headers.HasHeader("Content-Length")) {
            size_t contentLength;
            if (!ParseSize(request->headers.GetHeaderValue("Content-Length"), contentLength)) { 
                request->validity = Request::Validity::InvalidUnrecoverable;
                return request;
            }
            if (contentLength > MAX_CONTENT_LENGTH) {
                request->validity = Request::Validity::InvalidUnrecoverable;
                return request;
            } 
            if (contentLength > bodyAvailableSize) {
                return nullptr;
            } else {
                request->body = rawRequest.substr(bodyOffset, contentLength);
                messageEnd = bodyOffset + contentLength;
            }
        } else {
            //Finally, extract the body
            request->body.clear();
            messageEnd = bodyOffset;
        }
        
        return request;
    }
    

    void PrintTo(
        const Server::Request::Validity& validity,
        std::ostream* os
    ) {
        switch (validity) {
            case Server::Request::Validity::Valid: {
                *os << "VALID";
            } break;
            case Server::Request::Validity::ValidIncomplete: {
                *os << "VALID (Incomplete)";
            } break;
            case Server::Request::Validity::InvalidRecoverable: {
                *os << "INVALID (Recoverable)";
            } break;
            case Server::Request::Validity::InvalidUnrecoverable: {
                *os << "INVALID (Unrecoverable)";
            } break;
            default: {
                *os << "???";
            }
        }
    }

    bool Server::Mobilize(
        std::shared_ptr< ServerTransportLayer > transport,
        uint16_t port
    ) {
        impl_->transport = transport;
        if (!impl_->transport->BindNetwork(
            port,
            [this](std::shared_ptr< Connection > connection) {
                impl_->NewConnection(connection);
            }
        )) {
            impl_->transport = nullptr;
            return false;
        }
        return true;
    }

    void Server::Demobilize() {
        if (impl_->transport != nullptr) {
            impl_->transport->ReleaseNetwork();
            impl_->transport = nullptr;
        }
    }
}
