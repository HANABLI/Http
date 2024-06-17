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
                const std::string cannedResponse = (
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Length: 13\r\n"
                    "Content-Type: text/plain\r\n"
                    "\r\n"
                    "BadRequest.\r\n"
                );
                connectionState->connection->SendData(
                    std::vector< uint8_t >(
                        cannedResponse.begin(),
                        cannedResponse.end()
                    )
                );
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
            request->body.clear();
            messageEnd = bodyOffset;
        }
        
        return request;
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