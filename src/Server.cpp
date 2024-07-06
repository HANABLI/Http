/**
 * @file Server.cpp
 * 
 * This module is an implementation of the
 * Http::Server class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <set>
#include <inttypes.h>
#include <string>
#include <Http/Server.hpp>
#include <condition_variable>
#include <thread>
#include <memory>
#include <mutex>

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
         * These are the client connections that have been broken an will
         * be destroyed by the reaper thread.
         */
        std::set< std::shared_ptr< ConnectionState > > brokenConnections;

        /**
         * This is a helper object used to generate and publish
         * diagnostic messages.
         */
        SystemUtils::DiagnosticsSender diagnosticsSender;

        /**
         * This is a worker thread whose jobe is to clear the
         * brokenConnections set. The reason we need to put broken
         * connections there in the first place is because we can't
         * destroy a connection that is in the process of calling
         * us through one of the delegates we gave it.
         */
        std::thread reaper;

        /**
         * This flag indicates whether or not the reaper thread should stop.
         */
        bool stopReaper = false;

        /**
         * This is used to synchronize access to the server.
         */
        std::mutex mutex;

        /**
         * This is used by the reaper thread to wait on any
         * condition that it should cause it to wake up.
         */
        std::condition_variable condition;

        /**
         * This is the constructor for the structure
         */
        Impl(): diagnosticsSender("Http::Server"){

        }

        /**
         * This method is the body of the reaper thread.
         * Until it's told to stop, it simply clears the
         * brokenConnections set whenever it wakes up.
         */
        void Reaper() {
            std::unique_lock< decltype(mutex) > lock(mutex);
            while (!stopReaper) {
                std::set< std::shared_ptr< ConnectionState > > oldBrokenConnections(std::move(brokenConnections));
                brokenConnections.clear();
                {
                    lock.unlock();
                    oldBrokenConnections.clear();
                    lock.lock();
                }
                condition.wait(
                    lock,
                    [this]{
                        return (
                            stopReaper || !brokenConnections.empty()
                        );
                    }
                );
            }
        }


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
                unsigned int statusCode;
                std::string reasonPhrase;
                if (request->validity == Request::Validity::Valid) {
                    diagnosticsSender.SendDiagnosticInformationFormatted(
                        1,
                        "Received %s request for '%s' from %s",
                        request->method.c_str(),
                        request->target.GenerateString().c_str(),
                        connectionState->connection->GetPeerId().c_str()
                    );
                    const std::string cannedResponse = (
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: 13\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "BadRequest.\r\n"
                    );
                    response = cannedResponse;
                    statusCode = 404;
                    reasonPhrase = "Not Found";
                } else {
                    const std::string cannedResponse = (
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Length: 13\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "BadRequest.\r\n"
                    );
                    response = cannedResponse;
                    statusCode = 400;
                    reasonPhrase = "Bad Request";
                }

                connectionState->connection->SendData(
                    std::vector< uint8_t >(
                        response.begin(),
                        response.end()
                    )
                );
                diagnosticsSender.SendDiagnosticInformationFormatted(
                    1,
                    "Sent %u '%s' response back to %s",
                    statusCode,
                    reasonPhrase.c_str(),
                    connectionState->connection->GetPeerId().c_str()
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
            std::lock_guard< decltype(mutex) > lock(mutex);
            diagnosticsSender.SendDiagnosticInformationFormatted(
                2,
                "New connection from %s",
                connection->GetPeerId().c_str()
            );
            const auto connectionState = std::make_shared< ConnectionState >();
            connectionState->connection = connection;
            (void)establishedConnections.insert(connectionState);
            std::weak_ptr< ConnectionState > connectionStateWeak(connectionState);
            connectionState->connection->SetDataReceivedDelegate(
                [this, connectionStateWeak](std::vector< uint8_t > data){
                    std::lock_guard< decltype(mutex) > lock(mutex);
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
            connection->SetConnectionBrokenDelegate(
                [this, connectionStateWeak](bool graceful){
                    std::lock_guard< decltype(mutex) > lock(mutex);
                    const auto connectionState = connectionStateWeak.lock();
                    if (connectionState == nullptr) {
                        return;
                    }
                    diagnosticsSender.SendDiagnosticInformationFormatted(
                        2, 
                        "Connection to %s is broken by peer",
                        connectionState->connection->GetPeerId().c_str()
                    );
                    (void)brokenConnections.insert(connectionState);
                    condition.notify_all();
                    (void)establishedConnections.erase(connectionState);
                }
            );
        }
    };

    Server::~Server() {
        Demobilize();
        {
            std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
            impl_->stopReaper = true;
            impl_->condition.notify_all();
        }
        impl_->reaper.join();
    };

    Server::Server()
         : impl_(new Impl) {
            impl_->reaper = std::thread(&Impl::Reaper, impl_.get());
    }

    SystemUtils::DiagnosticsSender::UnsubscribeDelegate Server::SubscribeToDiagnostics(
        SystemUtils::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel 
    ){
        return impl_->diagnosticsSender.SubscribeToDiagnostics(delegate, minLevel);
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
        if (impl_->transport->BindNetwork(
            port,
            [this](std::shared_ptr< Connection > connection) {
                impl_->NewConnection(connection);
            }
        )) { 
            impl_->diagnosticsSender.SendDiagnosticInformationFormatted(
                3,
                "Now listening on port %" PRIu16,
                port
            );
        } else {
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
