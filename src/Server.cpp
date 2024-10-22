/**
 * @file Server.cpp
 * 
 * This module is an implementation of the
 * Http::Server class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <set>
#include <map>
#include <deque>
#include <inttypes.h>
#include <StringUtils/StringUtils.hpp>
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
    constexpr size_t DEFAULT_HEADER_LINE_LIMIT = 1000;

    enum ParseSizeResult {
        /**
         * this indicates the size was parsed successfully. 
         */
        Success,
        /**
         * This indicates that the size isn't a number.
         */
        NotaNumber,
        /**
         * This indicates a size overflow detection.
         */
        Overflow
    };

    /**
     * @return 
     *      An indication of whether or not the number was parsed
     *      successfully is returned
     */
    ParseSizeResult ParseSize(
        const std::string& stringSize,
        size_t& number 
    ) {
        number = 0;
        for ( auto c : stringSize ) {
            if (
                (c < '0')
                || (c > '9')
            ) { 
                return ParseSizeResult::NotaNumber;
            }
            auto previousNumber = number;
            number *= 10;
            number += (uint16_t)(c - '0');
            if (
                (number / 10) != previousNumber
            ) {
                return ParseSizeResult::Overflow;
            }
        }
        return ParseSizeResult::Success;
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

        /**
         * This is the state of the next request, while it's still
         * being received and parsed.
         */
        std::shared_ptr< Http::Server::Request > nextRequest = std::make_shared< Http::Server::Request>();

    };

    /**
     * This is used to record what resources are currently supported
     * by the server, and through which handler delegates.
     */
    struct ResourceSpace
    {
        /**
         * This is the name of the resource space, used as the key
         * to find it amoung all the subspaces of the supersapce.
         */
        std::string name;

        /**
         * This is the delegate to call to handler any resource request
         * within this space. If nullptr, the space is divided into 
         * subspaces.
         */
        Http::Server::ResourceDelegate handler;

        /**
         * If the space is divided into subspaces, these are the subspaces
         * which have currently registered handler delegates
         */
        std::map< std::string, std::shared_ptr< ResourceSpace > > subspaces;

        /**
         * This points back to the resource superspace containing
         * this subspace. 
         */
        std::weak_ptr< ResourceSpace > superspace;
    };
    

}

namespace Http {

    /**
     * This contains the private properties of a Server instance
     */

    struct Server::Impl
    {

        // Properties

        /**
         * This is the pointer to the server who own the connection.
         */
        Server* server = nullptr;

        /**
         * this holds configuration items of the server.
         */
        std::map< std::string, std::string> configuration;

        /**
         * This is the header line limit parameter
         */
        size_t headerLineLimit = DEFAULT_HEADER_LINE_LIMIT;

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
         * This represents the entire space of resources under the server.
         */
        std::shared_ptr< ResourceSpace > resources;

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
         * This method appends the given data to the end of the concatenate
         * buffer, and then attempts to parse a request out of it.
         * 
         * @param[in] server
         *      This is the server that has this connection.
         * 
         * @return 
         *      The request parsed from the concatenate buffer is returned.
         * 
         * @retval nullptr
         *      This is returned if no request could be parsed from the 
         *      concatinate buffer
         */
        std::shared_ptr< Request > TryRequestAssembly(
            std::shared_ptr< ConnectionState > connectionState
        ) {
            const auto charactersAccepted =  ParseRequest(
                connectionState->nextRequest,
                connectionState->concatenateBuffer
            );
            connectionState->concatenateBuffer.erase(
                connectionState->concatenateBuffer.begin(),
                connectionState->concatenateBuffer.begin() + charactersAccepted
            );
            if (!connectionState->nextRequest->IsProcessed()) {
                return nullptr;
            }
            const auto request = connectionState->nextRequest;
            connectionState->nextRequest = std::make_shared< Request >();
            return request;
        }
        
        /**
         * This method is called one or more times to incrementally parses the given string as a raw Http request message.
         * If the string parses correctly, the equivalent Request is returned.
         * otherwise, nullptr is returned.
         * 
         * @param[in, out] request
         *      This is the request to parse.
         * 
         * @param[in] nextRawRequestPart
         *      This is the raw HTTP request message as a string.
         * 
         * @param[out] messageEnd
         *      This is the raw HTTP request message as a single string.
         * 
         * @return
         *      A count of the number of charachters that were taken from
         *      the given input string is treturned. Presumably,
         *      any charachters past this point belong to another message or
         *      are outside the scope of HTTP. 
         */
        size_t ParseRequest(
            std::shared_ptr< Request > request,
            const std::string& nextRawRequestPart
        ) {
            // Count the number of characters incorporated into 
            // the request object.
            size_t messageEnd = 0;
            if (request->state == Request::RequestParsingState::RequestLine) {
                // First, extarct the request line.
                const auto requestLineEnd = nextRawRequestPart.find(CRLF);
                if (requestLineEnd == std::string::npos) {
                    if (nextRawRequestPart.length() > headerLineLimit) {
                        request->state = Request::RequestParsingState::Error;
                        return messageEnd;
                    }
                    return messageEnd;
                }
                const auto requestLineLength = requestLineEnd;
                if (requestLineLength > headerLineLimit) {
                    request->state = Request::RequestParsingState::Error;
                    return messageEnd;
                }
                const auto requestLine = nextRawRequestPart.substr(0, requestLineLength);
                messageEnd = requestLineEnd + CRLF.length();
                request->state = Request::RequestParsingState::Headers;
                request->valid = ParseRequestLine(request, requestLine);
            }
            //Second, parse the message headers and identify where the body begins.
            
            if (request->state == Request::RequestParsingState::Headers) {
                request->headers.SetLineLimit(headerLineLimit);
                size_t bodyOffset = 0;
                const auto headersValidity = request->headers
                        .ParseRawMessage(
                            nextRawRequestPart.substr(messageEnd),
                            bodyOffset
                        );
                messageEnd += bodyOffset;
                switch (
                    headersValidity
                ) {
                    case MessageHeaders::MessageHeaders::State::Complete: {
                        // Done with parsing headers; next will be the body.
                        if (!request->headers.IsValid()) {
                            request->valid = false;
                        }
                        request->state = Request::RequestParsingState::Body;
                        // Check for "Host" header
                        if (request->headers.HasHeader("Host")) {
                            const auto requestHost = request->headers.GetHeaderValue("Host");
                            auto serverHost = configuration["Host"];
                            if (serverHost.empty()) {
                                serverHost = requestHost;
                            }
                            auto targetHost = request->target.GetHost();
                            if (targetHost.empty()) {
                                targetHost = serverHost;
                            }
                            if ((requestHost != targetHost) || (requestHost != serverHost)) {
                                request->valid = false;
                            }
                            // TODO: check that target host matches server host.
                        } else {
                            request->valid = false;
                        }
                    } break;

                    case MessageHeaders::MessageHeaders::State::Incomplete: {
                    } return messageEnd;
                    case MessageHeaders::MessageHeaders::State::Error: 
                    default: 
                    {
                        request->state = Request::RequestParsingState::Error;
                        return messageEnd;
                    }
                }
            }

            // Finally, extract the body.
            if (request->state == Request::RequestParsingState::Body) {
                // Check for "Content-Length" header, if present, use it to
                // determine how many characters should be in the body. 
                const auto bodyAvailableSize = nextRawRequestPart.length() - messageEnd;
                // If it containt "Content-Length" header, we carefully carve exactly
                // that number of cahracters out (and bail if we don't have anough) 
                if (request->headers.HasHeader("Content-Length")) {
                    size_t contentLength;
                    switch (ParseSize(request->headers.GetHeaderValue("Content-Length"), contentLength)) {

                        case ParseSizeResult::NotaNumber:
                        {
                            request->state = Request::RequestParsingState::Error;
                            return messageEnd;
                        } 
                        case ParseSizeResult::Overflow:
                        {
                            request->state = Request::RequestParsingState::Error;
                            request->responseStatusCode = 413;
                            request->responseStatusPhrase = "Payload Too Large";
                            return messageEnd;
                        } 
                        
                    }
                    if (contentLength > MAX_CONTENT_LENGTH) {
                        request->state = Request::RequestParsingState::Error;
                        request->responseStatusCode = 413;
                        request->responseStatusPhrase = "Payload Too Large";
                        return messageEnd;
                    } 
                    if (contentLength > bodyAvailableSize) {
                        request->state = Request::RequestParsingState::Body;
                        return messageEnd;
                    } else {
                        request->body = nextRawRequestPart.substr(messageEnd, contentLength);
                        messageEnd += contentLength;
                        request->state = Request::RequestParsingState::Complete;
                    }
                } else {
                    //Finally, extract the body
                    request->body.clear();
                    request->state = Request::RequestParsingState::Complete;
                }
            }
            return messageEnd;
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
                const auto request = TryRequestAssembly(connectionState);
                if (request == nullptr) {
                    break;
                }
                std::string responseText;
                unsigned int statusCode;
                std::string status;
                if (
                    (request->state == Request::RequestParsingState::Complete)
                    && request->valid
                ) {
                    diagnosticsSender.SendDiagnosticInformationFormatted(
                        1,
                        "Received %s request for '%s' from %s",
                        request->method.c_str(),
                        request->target.GenerateString().c_str(),
                        connectionState->connection->GetPeerId().c_str()
                    );
                    const auto originalResourcePath = request->target.GetPath();
                    std::deque< std::string > resourcePath(
                        originalResourcePath.begin(),
                        originalResourcePath.end()
                    );
                    if (
                        !resourcePath.empty()
                        && (resourcePath.front() == "")
                    ) {
                        (void)resourcePath.pop_front();
                    }
                    std::shared_ptr< ResourceSpace > resource = resources;
                    while (
                        (resource != nullptr) 
                        && !resourcePath.empty()
                    ) {
                        const auto subspaceEntry = resource->subspaces.find(resourcePath.front());
                        if (subspaceEntry == resource->subspaces.end()) {
                            break;
                        } else {
                            resource = subspaceEntry->second;
                            resourcePath.pop_front();
                        }
                    }
                    if (
                        (resource != nullptr)
                        && (resource->handler != nullptr)
                    ) {
                        request->target.SetPath({resourcePath.begin(), resourcePath.end()});
                        const auto response = resource->handler(request);
                        if(
                            !response->headers.HasHeader("Transfer-Encoding")
                            && !response->body.empty()
                            && !response->headers.HasHeader("Content-Length")
                        ) {
                            response->headers.AddHeader(
                                "Content-Length",
                                StringUtils::sprintf("%zu", response->body.length())
                            );
                        }
                        responseText = response->GenerateToString();
                        statusCode = response->statusCode;
                        status = response->status;  
                    } else {
                        const std::string cannedResponse = (
                            "HTTP/1.1 404 Not Found\r\n"
                            "Content-Length: 13\r\n"
                            "Content-Type: text/plain\r\n"
                            "\r\n"
                            "BadRequest.\r\n"
                        );
                        responseText = cannedResponse;
                        statusCode = 404;
                        status = "Not Found";
                    }    
                } else if (
                    (request->state == Request::RequestParsingState::Error)
                    && (request->responseStatusCode == 413)
                ){
                    const std::string response = (
                        StringUtils::sprintf("HTTP/1.1 %u %s\r\n"
                            "Content-Length: 13\r\n"
                            "Content-Type: text/plain\r\n"
                            "\r\n"
                            "BadRequest.\r\n",
                            request->responseStatusCode,
                            request->responseStatusPhrase.c_str()
                        )
                    );
                    responseText = response;
                    statusCode = request->responseStatusCode;
                    status = request->responseStatusPhrase;
                } else {
                    const std::string cannedResponse = (
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Length: 13\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "BadRequest.\r\n"
                    );
                    responseText = cannedResponse;
                    statusCode = 400;
                    status = "Bad Request";
                }

                connectionState->connection->SendData(
                    std::vector< uint8_t >(
                        responseText.begin(),
                        responseText.end()
                    )
                );
                diagnosticsSender.SendDiagnosticInformationFormatted(
                    1,
                    "Sent %u '%s' response back to %s",
                    statusCode,
                    status.c_str(),
                    connectionState->connection->GetPeerId().c_str()
                );
                if (request->state == Request::RequestParsingState::Complete) {
                    const auto connectionTockens = request->headers.GetHeaderMultiValues("Connection");
                    bool closeRequested = false;
                    for (const auto& connectionTocken: connectionTockens) {
                        if (connectionTocken == "close") {
                            closeRequested = true;
                            break;
                        }
                    }
                    if (closeRequested) {
                        connectionState->connection->Break(true);
                    }
                } else {
                    if(request->state == Request::RequestParsingState::Error) {
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

    bool Server::Request::IsProcessed() const {
        return (
            (state == RequestParsingState::Complete)
            || (state == RequestParsingState::Error)
        );
    }

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
            impl_->server = this;
            impl_->configuration["HeaderLineLimit"] = StringUtils::sprintf("%zu", DEFAULT_HEADER_LINE_LIMIT);
            impl_->reaper = std::thread(&Impl::Reaper, impl_.get());
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
    
    auto Server::ParseRequest(const std::string& rawRequest)-> std::shared_ptr< Request > {
            size_t messageEnd;
            return ParseRequest(rawRequest, messageEnd);
        }

    auto Server::ParseRequest(const std::string& rawRequest, size_t& messageEnd)-> std::shared_ptr< Request > {
        auto request = std::make_shared< Request >();
        messageEnd = impl_->ParseRequest(request, rawRequest);
        if (!request->IsProcessed()) {
            request = nullptr;
        }
        return request;
    }
    
     std::string Server::GetConfigurationItem(const std::string& key){
        const auto entry = impl_->configuration.find(key);
        if (entry == impl_->configuration.end()) {
            return "";
        } else {
            return entry->second;
        }
    }

    void Server::SetConfigurationItem(const std::string& key, const std::string& value) {
        impl_->configuration[key] = value;
        if (key == "HeaderLineLimit") {
            size_t newHeaderLineLimit;
            if (
                sscanf(
                    value.c_str(),
                    "%zu",
                    &newHeaderLineLimit
                ) == 1
            ) {
                impl_->diagnosticsSender.SendDiagnosticInformationFormatted(
                    0,
                    "Header line limit changed from %zu to %zu",
                    impl_->headerLineLimit,
                    newHeaderLineLimit
                );
                impl_->headerLineLimit = newHeaderLineLimit;
            }
        }
    }
    

    SystemUtils::DiagnosticsSender::UnsubscribeDelegate Server::SubscribeToDiagnostics(
        SystemUtils::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel 
    ){
        return impl_->diagnosticsSender.SubscribeToDiagnostics(delegate, minLevel);
    }

   
    auto Server::RegisterResource(
            const std::vector< std::string >& resourceSubspacePath, 
            ResourceDelegate resourceDelegate
    ) -> UnregistrationDelegate {
        std::shared_ptr< ResourceSpace > space =  impl_->resources;
        if (space == nullptr) {
            space = impl_->resources = std::make_shared< ResourceSpace >();
        }
        for (const auto& pathSegment: resourceSubspacePath) {
            if (space->handler != nullptr) {
                return nullptr;
            }
            std::shared_ptr< ResourceSpace > subspace;
            auto subspacesEntry = space->subspaces.find(pathSegment);
            if (subspacesEntry == space->subspaces.end()) {
                subspace = space->subspaces[pathSegment] = std::make_shared< ResourceSpace >();
                subspace->name = pathSegment;
                subspace->superspace = space;
            } else {
                subspace = subspacesEntry->second;
            }
            space = subspace;
        }
        if (
            space->handler == nullptr
            && (space->subspaces.empty())
        ) {
            space->handler = resourceDelegate;
            return [this, space]{
                auto currentSpace = space;
                currentSpace->handler = nullptr;
                for (;;) {
                    auto superspace = currentSpace->superspace.lock();
                    if (
                        (currentSpace->handler == nullptr)
                        && currentSpace->subspaces.empty()
                    ) {
                        if (superspace == nullptr) {
                            impl_->resources = nullptr;
                            break;
                        } else {
                            (void)superspace->subspaces.erase(currentSpace->name);
                        }
                    }
                    if ( 
                        (superspace != nullptr) 
                        && superspace->subspaces.empty()
                    ) {
                        currentSpace = superspace;
                    } else {
                        break;
                    }
                }   
            };
        } else {
            return nullptr;
        }
    }

    void PrintTo(
        const IServer::Request::RequestParsingState& state,
        std::ostream* os
    ) {
        switch (state) {
            case Server::Request::RequestParsingState::Complete: {
                *os << "COMPLETE";
            } break;
            case Server::Request::RequestParsingState::RequestLine: {
                *os << "REQUEST LINE";
            } break;
            case Server::Request::RequestParsingState::Headers: {
                *os << "HEADERS";
            } break;
            case Server::Request::RequestParsingState::Body: {
                *os << "BODY";
            } break;
            case Server::Request::RequestParsingState::Error: {
                *os << "Error";
            } break;
            default: {
                *os << "???";
            }
        }
    }


}
