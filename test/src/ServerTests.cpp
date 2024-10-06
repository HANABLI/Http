/**
 * @file ServerTests.cpp
 * 
 * This module contains tests units of the 
 * Http::Server class.
 */

#include <gtest/gtest.h>
#include <limits>
#include <Http/Server.hpp>
#include <Http/Client.hpp>
#include <Http/ServerTransportLayer.hpp>
#include <SystemUtils/DiagnosticsSender.hpp>
#include <StringUtils/StringUtils.hpp>
#include <Http/Connection.hpp>
#include <Uri/Uri.hpp>

namespace {

    /**
     * This is a fake client connection which is used to test the server.
     */
    struct MockConnection : public Http::Connection
    {
        // Properies

        /**
         * This indicates whether or not the mock connection is received
         * from the remote peer.
         */
        bool callingDelegate =  false;

        std::recursive_mutex callingDelegateMutex;

        /**
         * This is the delegate to call whenever data is received from
         * the remote peer.
         */
        DataReceivedDelegate dataReceivedDelegate;

        /**
         * This is the delegate to call whenever connection has been broken.
         */
        BrokenDelegate brokenDelegate;

        /**
         * This is the data received from the remote peer.
         */
        std::vector< uint8_t > dataReceived;


        /**
         * This flag is set if  the remote close the connection
         */
        bool broken = false;

        // Lifecycle management
        ~MockConnection() {
            std::lock_guard< decltype(callingDelegateMutex) > lock(callingDelegateMutex);
            if (callingDelegate) {
                *((int*)0) = 42; //force a crash (use in a death test)
            }
        }

        MockConnection(const MockConnection&) = delete;
        MockConnection(MockConnection&&) = delete;
        MockConnection& operator=(const MockConnection&) = delete;
        MockConnection& operator=(MockConnection&&) = delete;

        // Methods

        MockConnection() = default;

        // Http::Connection

        virtual std::string GetPeerId() override {
            return "mock-client";
        }

        virtual void SetDataReceivedDelegate(DataReceivedDelegate newDataReceivedDelegate) override {
            dataReceivedDelegate = newDataReceivedDelegate;
        }

        virtual void SetConnectionBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
            brokenDelegate = newBrokenDelegate;
        }

        virtual void SendData(std::vector< uint8_t > data) override {
            (void)dataReceived.insert(
                dataReceived.end(),
                data.begin(),
                data.end()
            );
        }

        virtual void Break(bool clean) override {
            broken = true;
        }
    };

    /**
     * This is a fake transport layer which is used to test the server.
     */
    struct MockTransport : public Http::ServerTransportLayer {
        // Properties

        /**
         * This flag indicates whether or not the transport layer
         * has been bound by the server.
         */
        bool bound = false;

        /**
         * This is the port number that the server bound on the 
         * transport layer.
         */
        uint16_t port = 0;

        NewConnectionDelegate connectionDelegate;
        // Methods

        // Http::ServerTransport

        virtual bool BindNetwork(
            uint16_t newPort,
            NewConnectionDelegate newConnectionDelegate
        ) override {
            port = newPort;
            connectionDelegate = newConnectionDelegate;
            bound = true;
            return true;
        }

        virtual uint16_t GetBoundPort() override {
            return port;
        }

        virtual void ReleaseNetwork() override {
            bound = false;
        }
    };
}

struct ServerTests : public ::testing::Test
{
    // Properties
    /**
     * This is the unit under test server.
     */
    Http::Server server;
    /**
     * These are the diagnostic messages that have been received 
     * from the unit under test.
     */
    std::vector< std::string > diagnosticMessages;
    /**
     * This is the delegate obtained when subscribing 
     * to receive diagnostic messages from the unit under test.
     * It's called to terminate the subscription.
     */
    SystemUtils::DiagnosticsSender::UnsubscribeDelegate diagnosticsUnsubscribeDelegate;
    // Methods


    // ::testing::Test

    virtual void SetUp() {   
        server.SubscribeToDiagnostics(
            [this](
                std::string senderName,
                size_t level,
                std::string message
            ){
                diagnosticMessages.push_back(
                    StringUtils::sprintf(
                        "%s[%zu]: %s",
                        senderName.c_str(),
                        level,
                        message.c_str()
                    )
                );
            },
            0
        );
    }

    virtual void TearDown() {
        server.Demobilize();
        //diagnosticsUnsubscribeDelegate();
        
    }
};

TEST_F(ServerTests, ServerTests_ParseGetRequest_Test) {
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->state, Http::Server::Request::RequestParsingState::Complete);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_EQ("GET", request->method);
    ASSERT_EQ(expectedUri, request->target);
    ASSERT_TRUE(request->headers.HasHeader("User-Agent"));
    ASSERT_EQ("curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3", request->headers.GetHeaderValue("User-Agent"));
    ASSERT_TRUE(request->headers.HasHeader("Host"));
    ASSERT_EQ("www.example.com", request->headers.GetHeaderValue("Host"));
    ASSERT_TRUE(request->headers.HasHeader("Accept-Language"));
    ASSERT_EQ("en, mi", request->headers.GetHeaderValue("Accept-Language"));
    ASSERT_TRUE(request->body.empty());
}


TEST_F(ServerTests, ServerTests_ParsePostRequest_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 27\r\n"
        "\r\n"
        "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/test");
    ASSERT_EQ("POST", request->method);
    ASSERT_EQ(expectedUri, request->target);
    ASSERT_TRUE(request->headers.HasHeader("Host"));
    ASSERT_EQ("foo.example", request->headers.GetHeaderValue("Host"));
    ASSERT_TRUE(request->headers.HasHeader("Content-Length"));
    ASSERT_EQ("27", request->headers.GetHeaderValue("Content-Length"));
    ASSERT_EQ("field1=value1&field2=value2", request->body);
    ASSERT_EQ(rawRequest.length() - 2, messageEnd);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestNoMethod_Test) {
    size_t messageEnd;
    const std::string rawRequest = (        
        " /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteBodyRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 50\r\n"
        "\r\n"
        "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteHeadersRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 50\r\n"
        "\r\n"
        "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteMidLineHeadersRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/x-www\r\n");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseNoBodyDelimiterRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 50\r\n"
    );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteRequestLine_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseNoUriRequest_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST / HTTP/1.1\r\n"
        "Host: foo.example\r\n"
        "Content-Type: application/");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, RequestWithNoContentLengthOrChunkedTransferEncodingHasNoBody) {
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        "Hello, World\r\n"
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->state, Http::Server::Request::RequestParsingState::Complete);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_TRUE(request->body.empty());
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestNoTarget_Test) {
    size_t messageEnd;
    const std::string rawRequest = (        
        "GET HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBadProtocol_Test) {
    size_t messageEnd;
    const std::string rawRequest = (        
        "GET /hello.txt Foo\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestDamageHeader_Test) {
    size_t messageEnd;
    const std::string rawRequest = (        
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
    ASSERT_EQ(rawRequest.length() ,messageEnd);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBodyExtremelyTooLarge_Test) {
    size_t messageEnd = std::numeric_limits< size_t >::max();
    const std::string rawRequest = (        
            "GET /hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: www.example.com\r\n"
            "Content-Length: 1000000000000000000000000000000000\r\n"
            "Accept-Language: en, mi\r\n"
            "\r\n"
        );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBodySligntlysTooLarge_Test) {
    size_t messageEnd;
    const std::string rawRequest = (        
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Content-Length: 10000001\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        );
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ParseValideHeaderLineLongerThanDefault) {
    size_t messageEnd;
    const std::string testHeaderName("X-Poggers");
    const std::string testHeaderNameWithDelimiters = testHeaderName + ": ";
    const std::string valueIsTooLong(999 - testHeaderNameWithDelimiters.length(), 'X');
    const std::string rawRequest = (
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        + testHeaderNameWithDelimiters + valueIsTooLong + "\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_EQ("1000", server.GetConfigurationItem("HeaderLineLimit"));
    server.SetConfigurationItem("HeaderLineLimit", "1001");
    ASSERT_EQ(
        (std::vector< std::string >{
            "Http::Server[0]: Header line limit changed from 1000 to 1001",
             }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    ASSERT_EQ("1001", server.GetConfigurationItem("HeaderLineLimit"));
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
}

TEST_F(ServerTests, ServerTests_Mobiliz_Test) {
    auto transport = std::make_shared< MockTransport >();
    ASSERT_TRUE(server.Mobilize(transport, 1234));
    ASSERT_EQ(1234, transport->port);
    ASSERT_FALSE(transport->connectionDelegate == nullptr);
}

TEST_F(ServerTests, ServerTests_Demobilize_Test) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    server.Demobilize();
    ASSERT_FALSE(transport->bound);
}

TEST_F(ServerTests, ServerTests_ReleaseNetworkUponDestruction_Test) {
    auto transport = std::make_shared< MockTransport >();
    {
        Http::Server tmpServer;
        (void)tmpServer.Mobilize(transport, 1234);
    }
    ASSERT_FALSE(transport->bound);
}

TEST_F(ServerTests, Expect404FromClientRequest) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    const std::string expectedResponse = (
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
      );
    ASSERT_EQ(
        expectedResponse,
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
}

TEST_F(ServerTests, ServerTests_Expect404FromClientRequestInTwoPieces__Test) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    ASSERT_EQ(
        (std::vector< std::string >{
            "Http::Server[3]: Now listening on port 1234",
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_EQ(
        (std::vector< std::string >{
            "Http::Server[2]: New connection from mock-client",
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin(),
            request.begin() + request.length() / 2
        )
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin() + request.length() / 2,
            request.end()
        )
    );
    const std::string expectedResponse = (
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
      );
    ASSERT_EQ(
        expectedResponse,
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            "Http::Server[1]: Received GET request for '/hello.txt' from mock-client",
            "Http::Server[1]: Sent 404 'Not Found' response back to mock-client",
        }), 
        diagnosticMessages
    );
}


TEST_F(ServerTests, twoClientRequestsInOnePiece) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    std::string requests(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            requests.begin(),
            requests.end()
        )
    );
    const std::string expectedResponses = (
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
      );
    ASSERT_EQ(
        expectedResponses,
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
}

TEST_F(ServerTests, ClientInvalidRequestRecoverable) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string requests(
        "POST /hello.txt HTTP/1.1\r\n"
        "User-Agent curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        "POST /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            requests.begin(),
            requests.end()
        )
    );
    const std::string expectedResponses = (
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
      );
    ASSERT_EQ(
        expectedResponses,
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    ASSERT_FALSE(connection->broken);
}

TEST_F(ServerTests, ClientInvalidRequestUnrecoverable) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string request(
        "POST /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Content-Length: 1300000000000000000000000000\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    const std::string expectedResponse = (
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "BadRequest.\r\n"
      );
    ASSERT_EQ(
        expectedResponse,
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    ASSERT_TRUE(connection->broken);
}

TEST_F(ServerTests, ServerTests_ClientConnectionBroken_Test) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->brokenDelegate == nullptr);
    diagnosticMessages.clear();
    connection->brokenDelegate(true);
    ASSERT_EQ(
        (std::vector< std::string >{
            "Http::Server[2]: Connection to mock-client is broken by peer",
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
}

TEST_F(ServerTests, ClientShouldNotBeRaleasedDuringBreakDelegateCall) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    auto connectionRaw = connection.get();
    connection = nullptr;
    {
        std::lock_guard< decltype(connectionRaw->callingDelegateMutex) > lock(connectionRaw->callingDelegateMutex);
        connectionRaw->callingDelegate = true;
        connectionRaw->brokenDelegate(true);
        connectionRaw->callingDelegate = false;
    }
    
}

TEST_F(ServerTests, ParseInvalidRequestLineTooLong) {
    size_t messageEnd;
    const std::string uriTooLong(1000, 'X');
    const std::string rawRequest = (
        "Get" + uriTooLong + " HTTP/1.1\r\n"
    );
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ConnectionCloseOrNot) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);

    for (int i = 0; i < 2; ++i) {
        const auto tellServertoCloseAfterResponse = (i == 0);
        const std::string connectionHeader = (
            tellServertoCloseAfterResponse ? "Connection: close\r\n" : ""
        );    
        auto connection = std::make_shared< MockConnection >();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET /hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: www.example.com\r\n"
            "Accept-Language: en, mi\r\n"
            + connectionHeader
            + "\r\n"
        );
        connection->dataReceivedDelegate(   
            std::vector< uint8_t >(
                request.begin(),
                request.end()
            )
        );
        if (tellServertoCloseAfterResponse) {
            EXPECT_TRUE(connection->broken) << "We asked the server to closed?" << tellServertoCloseAfterResponse;
        } else {
            EXPECT_FALSE(connection->broken) << "We asked the server to closed?" << tellServertoCloseAfterResponse;
        }
    }
}

TEST_F(ServerTests, ServerTests_HostMissing_Test) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    const std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    ASSERT_FALSE(response == nullptr);
    ASSERT_EQ(400, response->statusCode);
}

TEST_F(ServerTests, ServerTests_HostNotMatchingTargetUri_Test) {
    
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    const std::string request(
        "GET http://www.example.com/hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: bad.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    connection->dataReceivedDelegate(   
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    ASSERT_FALSE(response == nullptr);
    ASSERT_EQ(400, response->statusCode);
}

TEST_F(ServerTests, ServerTests_HostNotMatchingServerUri_Test) {
    ASSERT_EQ("", server.GetConfigurationItem("Host"));
    server.SetConfigurationItem("Host", "www.example.com");
    struct TestVector
    {
        std::string hostUri;
        bool badRequestStatusExpected;
    };
    std::vector< TestVector > testVectors{
        {"www.example.com", false},
        {"bad.example.com", true}
    };
    size_t index = 0;
    for (const auto& testVector: testVectors) {
        auto transport = std::make_shared< MockTransport >();
        (void)server.Mobilize(transport, 1234);
        auto connection = std::make_shared< MockConnection >();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET http://www.example.com/hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: " + testVector.hostUri + "\r\n"
            "Accept-Language: en, mi\r\n"
            "\r\n"
        );
        connection->dataReceivedDelegate(   
            std::vector< uint8_t >(
                request.begin(),
                request.end()
            )
        );
        Http::Client client;
        const auto response = client.ParseResponse(
            std::string(
                connection->dataReceived.begin(),
                connection->dataReceived.end()
            )
        );
        ASSERT_FALSE(response == nullptr);
        if (testVector.badRequestStatusExpected) {
            EXPECT_EQ(400, response->statusCode) << "Fail for test vector index" << index;
        } else {
            EXPECT_NE(400, response->statusCode) << "Fail for test vector index" << index;
        }
        ASSERT_FALSE(connection->broken) << "Failed for test vector index" << index;
        index++;
    }
}

TEST_F(ServerTests, ServerTests_DefaultServerUri_Test) {
    ASSERT_EQ("", server.GetConfigurationItem("Host"));
    const std::vector< std::string > testVectors{
        "www.example.com",
        "bad.example.com"
    };
    size_t index = 0;
    for (const auto testVector: testVectors) { 
        auto transport = std::make_shared< MockTransport >();
        (void)server.Mobilize(transport, 1234);
        auto connection = std::make_shared< MockConnection >();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET /hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: " + testVector + "\r\n"
            "Accept-Language: en, mi\r\n"
            "\r\n"
        );
        connection->dataReceivedDelegate(   
            std::vector< uint8_t >(
                request.begin(),
                request.end()
            )
        );
        Http::Client client;
        const auto response = client.ParseResponse(
            std::string(
                connection->dataReceived.begin(),
                connection->dataReceived.end()
            )
        );
        ASSERT_FALSE(response == nullptr);
        EXPECT_NE(400, response->statusCode) << "Failed for test viector index" << index;
        index++;
    }
}

TEST_F(ServerTests, ServerTests_RegisterResourceDelegate__Test) {
    auto transport = std::make_shared< MockTransport >();
    (void)server.Mobilize(transport, 1234);
    auto connection = std::make_shared < MockConnection >();
    transport->connectionDelegate(connection);

    const std::string request = (
        "GET /foo/bar HTTP/1.1\r\n"
        "Host: www.exemple.com\r\n"
        "\r\n"
    );
    connection->dataReceivedDelegate(
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    Http::Client client;
    auto response = client.ParseResponse(
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();

    std::vector< Uri::Uri > RequestsResived;
    const auto resourceDelegate = [&RequestsResived](
        std::shared_ptr< Http::Server::Request > request
    ){
        const auto response = std::make_shared< Http::Client::Response >();
        RequestsResived.push_back(request->target);
        return response;
    };
    const auto unregistrationDelegate = server.RegisterResource({"foo"}, resourceDelegate);
    ASSERT_TRUE(RequestsResived.empty());
    connection->dataReceivedDelegate(
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    response = client.ParseResponse(
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
   
    EXPECT_EQ(200, response->statusCode);
    ASSERT_EQ(1, RequestsResived.size());
    ASSERT_EQ(
        (std::vector< std::string >{ "bar" }),
        RequestsResived[0].GetPath()
    );
    connection->dataReceived.clear();
    unregistrationDelegate();
    connection->dataReceivedDelegate(
        std::vector< uint8_t >(
            request.begin(),
            request.end()
        )
    );
    response = client.ParseResponse(
        std::string(
            connection->dataReceived.begin(),
            connection->dataReceived.end()
        )
    );
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();
}