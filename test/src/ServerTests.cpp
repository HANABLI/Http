/**
 * @file ServerTests.cpp
 * 
 * This module contains tests units of the 
 * Http::Server class.
 */

#include <gtest/gtest.h>
#include <Http/Server.hpp>
#include <Http/ServerTransportLayer.hpp>
#include <Uri/Uri.hpp>

namespace {

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

        virtual void SetNewConnectionDelegate(NewConnectionDelegate newConnectionDelegate) {
            connectionDelegate = newConnectionDelegate;
        }

        virtual bool BindNetwork(
            uint16_t newPort,
            NewConnectionDelegate newConnectionDelegate
        ) override {
            port = newPort;
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
        "Content-Type: application/");
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
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_TRUE(request->body.empty());
}

TEST(ServerTests, ServerTests_Mobiliz_Test) {
    auto transport = std::make_shared< MockTransport >();
    Http::Server server;
    ASSERT_TRUE(server.Mobilize(transport, 1234));
    ASSERT_EQ(1234, transport->port);
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