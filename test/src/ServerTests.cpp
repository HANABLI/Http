/**
 * @file ServerTests.cpp
 * 
 * This module contains tests units of the 
 * Http::Server class.
 */

#include <gtest/gtest.h>
#include <limits>
#include <Http/Server.hpp>
#include <Http/ServerTransportLayer.hpp>
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

        // Methods

        // Http::Connection

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

        virtual void ReleaseNetwork() override {
            bound = false;
        }
    };
}

TEST(ServerTests, ServerTests_ParseGetRequest_Test) {
    Http::Server server;
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->validity, Http::Server::Request::Validity::Valid);
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


TEST(ServerTests, ServerTests_ParsePostRequest_Test) {
    Http::Server server;
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

TEST(ServerTests, ServerTests_ParseInvalidRequestNoMethod_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidRecoverable, request->validity);
}

TEST(ServerTests, ServerTests_ParseIncompleteBodyRequ_Test) {
    Http::Server server;
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

TEST(ServerTests, ServerTests_ParseIncompleteHeadersRequ_Test) {
    Http::Server server;
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

TEST(ServerTests, ServerTests_ParseIncompleteMidLineHeadersRequ_Test) {
    Http::Server server;
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

TEST(ServerTests, ServerTests_ParseNoBodyDelimiterRequ_Test) {
    Http::Server server;
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

TEST(ServerTests, ServerTests_ParseIncompleteRequestLine_Test) {
    Http::Server server;
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.");
    const auto request = server.ParseRequest(
        rawRequest,
        messageEnd
    );
    ASSERT_TRUE(request == nullptr);
}

TEST(ServerTests, ServerTests_ParseNoUriRequest_Test) {
    Http::Server server;
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

TEST(ServerTests, RequestWithNoContentLengthOrChunkedTransferEncodingHasNoBody) {
    Http::Server server;
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        "Hello, World\r\n"
    );
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->validity, Http::Server::Request::Validity::Valid);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_TRUE(request->body.empty());
}

TEST(ServerTests, ServerTests_ParseInvalidRequestNoTarget_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidRecoverable, request->validity);
}

TEST(ServerTests, ServerTests_ParseInvalidRequestBadProtocol_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidRecoverable, request->validity);
}

TEST(ServerTests, ServerTests_ParseInvalidRequestDamageHeader_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidRecoverable, request->validity);
}

TEST(ServerTests, ServerTests_ParseInvalidRequestBodyExtremelyTooLarge_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidUnrecoverable, request->validity);
    ASSERT_EQ(0, messageEnd);
}

TEST(ServerTests, ServerTests_ParseInvalidRequestBodySligntlysTooLarge_Test) {
    Http::Server server;
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
    ASSERT_EQ(Http::Server::Request::Validity::InvalidUnrecoverable, request->validity);
}

TEST(ServerTests, ServerTests_Mobiliz_Test) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server server;
    ASSERT_TRUE(server.Mobilize(transport, 1234));
    ASSERT_EQ(1234, transport->port);
    ASSERT_FALSE(transport->connectionDelegate == nullptr);
}

TEST(ServerTests, ServerTests_Demobilize_Test) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server server;
    (void)server.Mobilize(transport, 1234);
    server.Demobilize();
    ASSERT_FALSE(transport->bound);
}

TEST(ServerTests, ServerTests_ReleaseNetworkUponDestruction_Test) {
    auto transport = std::make_shared< MockTransport >();
    {
        Http::Server server;
        (void)server.Mobilize(transport, 1234);
    }
    ASSERT_FALSE(transport->bound);
}

TEST(ServerTests, Expect404FromClientRequest) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server Server;
    (void)Server.Mobilize(transport, 1234);
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

TEST(ServerTests, ServerTests_Expect404FromClientRequestInTwoPieces__Test) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server Server;
    (void)Server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
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
}


TEST(ServerTests, twoClientRequestsInOnePiece) {
        auto transport = std::make_shared< MockTransport >();
    Http::Server Server;
    (void)Server.Mobilize(transport, 1234);
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

TEST(ServerTests, ClientInvalidRequestRecoverable) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server Server;
    (void)Server.Mobilize(transport, 1234);
    auto connection = std::make_shared< MockConnection >();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string request(
        "POST /hello.txt HTTP/1.1\r\n"
        "User-Agent curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
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
    ASSERT_FALSE(connection->broken);
}

TEST(ServerTests, ClientInvalidRequestUnrecoverable) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server Server;
    (void)Server.Mobilize(transport, 1234);
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