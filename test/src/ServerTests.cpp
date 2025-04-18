/**
 * @file ServerTests.cpp
 *
 * This module contains tests units of the
 * Http::Server class.
 */

#include <gtest/gtest.h>
#include <Http/Client.hpp>
#include <Http/Connection.hpp>
#include <Http/Server.hpp>
#include <Http/ServerTransportLayer.hpp>
#include <StringUtils/StringUtils.hpp>
#include <SystemUtils/DiagnosticsSender.hpp>
#include <Uri/Uri.hpp>
#include <condition_variable>
#include <limits>

namespace
{
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
        bool callingDelegate = false;

        /**
         * This is a function to be called when the mock connection
         * is destroyed
         */
        std::function<void()> onDestruction;

        /**
         * This is used to sysnchronize access to the wait condition.
         */
        std::recursive_mutex mutex;

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
        std::vector<uint8_t> dataReceived;

        /**
         * This is used to wait for, or signal, a condition
         * upon which that the tests might be wating.
         */
        std::condition_variable_any waitCondition;

        /**
         * This flag is set if  the remote close the connection
         */
        bool broken = false;

        // Lifecycle management
        ~MockConnection() {
            std::lock_guard<decltype(mutex)> lock(mutex);
            if (callingDelegate)
            {
                *((int*)0) = 42;  // force a crash (use in a death test)
            }
            if (onDestruction != nullptr)
            { onDestruction(); }
        }

        MockConnection(const MockConnection&) = delete;
        MockConnection(MockConnection&&) = delete;
        MockConnection& operator=(const MockConnection&) = delete;
        MockConnection& operator=(MockConnection&&) = delete;

        // Methods

        MockConnection() = default;

        /**
         * This method waits for the server to return a complete
         * response.
         *
         * @return
         *     An indication of whether or not a complete
         *     response was returned by the server before a reasonable
         *     timeout period has elapsed is returned.
         */
        bool AwaitResponse() {
            std::unique_lock<decltype(mutex)> lock(mutex);
            return waitCondition.wait_for(lock, std::chrono::milliseconds(100),
                                          [this] { return !dataReceived.empty(); });
        }
        /**
         * This method waits for the server to break the connection
         *
         * @return
         *      An indication of wether or not the connection was broken
         *      by the server before a reasonable timeout period has elapsed
         *      is returned.
         */
        bool AwaitBroken() {
            std::unique_lock<decltype(mutex)> lock(mutex);
            return waitCondition.wait_for(lock, std::chrono::milliseconds(100),
                                          [this] { return broken; });
        }
        // Http::Connection

        virtual std::string GetPeerId() override { return "mock-client"; }

        virtual void SetDataReceivedDelegate(
            DataReceivedDelegate newDataReceivedDelegate) override {
            dataReceivedDelegate = newDataReceivedDelegate;
        }

        virtual void SetConnectionBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
            brokenDelegate = newBrokenDelegate;
        }

        virtual void SendData(const std::vector<uint8_t>& data) override {
            std::lock_guard<decltype(mutex)> lock(mutex);
            (void)dataReceived.insert(dataReceived.end(), data.begin(), data.end());
            waitCondition.notify_all();
        }

        virtual void Break(bool clean) override {
            std::lock_guard<decltype(mutex)> lock(mutex);
            broken = true;
            waitCondition.notify_all();
        }
    };

    /**
     * This is a fake transport layer which is used to test the server.
     */
    struct MockTransport : public Http::ServerTransportLayer
    {
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

        virtual bool BindNetwork(uint16_t newPort,
                                 NewConnectionDelegate newConnectionDelegate) override {
            port = newPort;
            connectionDelegate = newConnectionDelegate;
            bound = true;
            return true;
        }

        virtual uint16_t GetBoundPort() override { return port; }

        virtual void ReleaseNetwork() override { bound = false; }
    };

    struct MockTimeKeeper : public Http::TimeKeeper
    {
        // Properties
        double currentTime = 0.0;

        // Methods
    public:
        /**
         * This method returns the current server time.
         *
         * @return
         *      The current server time is returned in seconds.
         */
        virtual double GetCurrentTime() override { return currentTime; }
    };
}  // namespace

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
    std::vector<std::string> diagnosticMessages;
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
            [this](std::string senderName, size_t level, std::string message)
            {
                diagnosticMessages.push_back(StringUtils::sprintf("%s[%zu]: %s", senderName.c_str(),
                                                                  level, message.c_str()));
            },
            0);
    }

    virtual void TearDown() {
        server.Demobilize();
        // diagnosticsUnsubscribeDelegate();
    }
};

TEST_F(ServerTests, ServerTests_ParseGetRequest_Test) {
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->state, Http::Server::Request::RequestParsingState::Complete);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_EQ("GET", request->method);
    ASSERT_EQ(expectedUri, request->target);
    ASSERT_TRUE(request->headers.HasHeader("User-Agent"));
    ASSERT_EQ("curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3",
              request->headers.GetHeaderValue("User-Agent"));
    ASSERT_TRUE(request->headers.HasHeader("Host"));
    ASSERT_EQ("www.example.com", request->headers.GetHeaderValue("Host"));
    ASSERT_TRUE(request->headers.HasHeader("Accept-Language"));
    ASSERT_EQ("en, mi", request->headers.GetHeaderValue("Accept-Language"));
    ASSERT_TRUE(request->body.empty());
}

TEST_F(ServerTests, ServerTests_ParsePostRequest_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST /test HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 27\r\n"
         "\r\n"
         "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
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
    const std::string rawRequest =
        (" /hello.txt HTTP/1.1\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteBodyRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST /test HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 50\r\n"
         "\r\n"
         "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteHeadersRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST /test HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 50\r\n"
         "\r\n"
         "field1=value1&field2=value2\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteMidLineHeadersRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST /test HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/x-www\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseNoBodyDelimiterRequ_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST /test HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 50\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseIncompleteRequestLine_Test) {
    size_t messageEnd;
    const std::string rawRequest = ("POST /test HTTP/1.");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, ServerTests_ParseNoUriRequest_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("POST / HTTP/1.1\r\n"
         "Host: foo.example\r\n"
         "Content-Type: application/");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_TRUE(request == nullptr);
}

TEST_F(ServerTests, RequestWithNoContentLengthOrChunkedTransferEncodingHasNoBody) {
    const auto request = server.ParseRequest(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n"
        "Hello, World\r\n");
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(request->state, Http::Server::Request::RequestParsingState::Complete);
    Uri::Uri expectedUri;
    expectedUri.ParseFromString("/hello.txt");
    ASSERT_TRUE(request->body.empty());
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestNoTarget_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("GET HTTP/1.1\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBadProtocol_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("GET /hello.txt Foo\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestDamageHeader_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("GET /hello.txt HTTP/1.1\r\n"
         "User-Agent curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
    ASSERT_FALSE(request->valid);
    ASSERT_EQ(rawRequest.length(), messageEnd);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBodyExtremelyTooLarge_Test) {
    size_t messageEnd = std::numeric_limits<size_t>::max();
    const std::string rawRequest =
        ("GET /hello.txt HTTP/1.1\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Content-Length: 1000000000000000000000000000000000\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ServerTests_ParseInvalidRequestBodySligntlysTooLarge_Test) {
    size_t messageEnd;
    const std::string rawRequest =
        ("GET /hello.txt HTTP/1.1\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
         "Host: www.example.com\r\n"
         "Content-Length: 10000001\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ParseValideHeaderLineLongerThanDefault) {
    size_t messageEnd;
    const std::string testHeaderName("X-Poggers");
    const std::string testHeaderNameWithDelimiters = testHeaderName + ": ";
    const std::string valueIsTooLong(999 - testHeaderNameWithDelimiters.length(), 'X');
    const std::string rawRequest =
        ("GET /hello.txt HTTP/1.1\r\n"
         "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n" +
         testHeaderNameWithDelimiters + valueIsTooLong +
         "\r\n"
         "Host: www.example.com\r\n"
         "Accept-Language: en, mi\r\n"
         "\r\n");
    ASSERT_EQ("1000", server.GetConfigurationItem("HeaderLineLimit"));
    server.SetConfigurationItem("HeaderLineLimit", "1001");
    ASSERT_EQ((std::vector<std::string>{
                  "Http::Server[0]: Header line limit changed from 1000 to 1001",
              }),
              diagnosticMessages);
    diagnosticMessages.clear();
    ASSERT_EQ("1001", server.GetConfigurationItem("HeaderLineLimit"));
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Complete, request->state);
}

TEST_F(ServerTests, ServerTests_Mobiliz_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    server.SetConfigurationItem("Port", "1234");
    server.SetConfigurationItem("RequestTimeout", "1.0");
    server.SetConfigurationItem("InactivityTimeout", "1.0");
    ASSERT_TRUE(server.Mobilize(dep));
    ASSERT_EQ(1234, transport->port);
    ASSERT_FALSE(transport->connectionDelegate == nullptr);
}

TEST_F(ServerTests, ServerTests_Demobilize_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    server.Demobilize();
    ASSERT_FALSE(transport->bound);
}

TEST_F(ServerTests, ServerTests_ReleaseNetworkUponDestruction_Test) {
    auto transport = std::make_shared<MockTransport>();
    {
        Http::Server tmpServer;
        auto timeKeeper = std::make_shared<MockTimeKeeper>();
        const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
        (void)tmpServer.Mobilize(dep);
    }
    ASSERT_FALSE(transport->bound);
}

TEST_F(ServerTests, Expect404FromClientRequest) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    ASSERT_TRUE(server.Mobilize(dep));
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    const std::string expectedResponse =
        ("HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n");
    ASSERT_EQ(expectedResponse,
              std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
}

TEST_F(ServerTests, ServerTests_Expect404FromClientRequestInTwoPieces__Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    server.SetConfigurationItem("Port", "1234");
    diagnosticMessages.clear();
    (void)server.Mobilize(dep);
    ASSERT_EQ((std::vector<std::string>{
                  "Http::Server[3]: Now listening on port 1234",
              }),
              diagnosticMessages);
    diagnosticMessages.clear();
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    ASSERT_EQ((std::vector<std::string>{
                  "Http::Server[2]: New connection from mock-client",
              }),
              diagnosticMessages);
    diagnosticMessages.clear();
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(
        std::vector<uint8_t>(request.begin(), request.begin() + request.length() / 2));
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(
        std::vector<uint8_t>(request.begin() + request.length() / 2, request.end()));
    const std::string expectedResponse =
        ("HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n");
    ASSERT_EQ(expectedResponse,
              std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    ASSERT_EQ((std::vector<std::string>{
                  "Http::Server[1]: Received GET request for '/hello.txt' from "
                  "mock-client",
                  "Http::Server[1]: Sent 404 'Not Found' response back to mock-client",
              }),
              diagnosticMessages);
}

TEST_F(ServerTests, twoClientRequestsInOnePiece) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
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
        "\r\n");
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(requests.begin(), requests.end()));
    const std::string expectedResponses =
        ("HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n"
         "HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n");
    ASSERT_EQ(expectedResponses,
              std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
}

TEST_F(ServerTests, ClientInvalidRequestRecoverable) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
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
        "\r\n");
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(requests.begin(), requests.end()));
    const std::string expectedResponses =
        ("HTTP/1.1 400 Bad Request\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n"
         "HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n");
    ASSERT_EQ(expectedResponses,
              std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    ASSERT_FALSE(connection->broken);
}

TEST_F(ServerTests, ClientInvalidRequestUnrecoverable) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->dataReceivedDelegate == nullptr);
    const std::string request(
        "POST /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Content-Length: 1300000000000000000000000000\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    ASSERT_TRUE(connection->dataReceived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    const std::string expectedResponse =
        ("HTTP/1.1 413 Payload Too Large\r\n"
         "Content-Type: text/plain\r\n"
         "Connection: close\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "BadRequest.\r\n");
    ASSERT_EQ(expectedResponse,
              std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    ASSERT_TRUE(connection->broken);
}

TEST_F(ServerTests, ServerTests_ClientConnectionBroken_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    ASSERT_FALSE(connection->brokenDelegate == nullptr);
    diagnosticMessages.clear();
    connection->brokenDelegate(true);
    ASSERT_EQ((std::vector<std::string>{
                  "Http::Server[2]: Connection to mock-client is broken by peer",
              }),
              diagnosticMessages);
    diagnosticMessages.clear();
}

TEST_F(ServerTests, ClientShouldNotBeRaleasedDuringBreakDelegateCall) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    auto connectionRaw = connection.get();
    connection = nullptr;
    {
        std::lock_guard<decltype(connectionRaw->mutex)> lock(connectionRaw->mutex);
        connectionRaw->callingDelegate = true;
        connectionRaw->brokenDelegate(true);
        connectionRaw->callingDelegate = false;
    }
}

TEST_F(ServerTests, ParseInvalidRequestLineTooLong) {
    size_t messageEnd;
    const std::string uriTooLong(1000, 'X');
    const std::string rawRequest = ("Get" + uriTooLong + " HTTP/1.1\r\n");
    const auto request = server.ParseRequest(rawRequest, messageEnd);
    ASSERT_FALSE(request == nullptr);
    ASSERT_EQ(Http::Server::Request::RequestParsingState::Error, request->state);
}

TEST_F(ServerTests, ConnectionCloseOrNot) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);

    for (int i = 0; i < 2; ++i)
    {
        const auto tellServertoCloseAfterResponse = (i == 0);
        const std::string connectionHeader =
            (tellServertoCloseAfterResponse ? "Connection: close\r\n" : "");
        auto connection = std::make_shared<MockConnection>();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET /hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: www.example.com\r\n"
            "Accept-Language: en, mi\r\n" +
            connectionHeader + "\r\n");
        connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
        if (tellServertoCloseAfterResponse)
        {
            EXPECT_TRUE(connection->broken)
                << "We asked the server to closed?" << tellServertoCloseAfterResponse;
        } else
        {
            EXPECT_FALSE(connection->broken)
                << "We asked the server to closed?" << tellServertoCloseAfterResponse;
        }
    }
}

TEST_F(ServerTests, ServerTests_HostMissing_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    const std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    ASSERT_FALSE(response == nullptr);
    ASSERT_EQ(400, response->statusCode);
}

TEST_F(ServerTests, ServerTests_HostNotMatchingTargetUri_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    const std::string request(
        "GET http://www.example.com/hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: bad.example.com\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
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
    std::vector<TestVector> testVectors{{"www.example.com", false}, {"bad.example.com", true}};
    size_t index = 0;
    for (const auto& testVector : testVectors)
    {
        auto transport = std::make_shared<MockTransport>();
        auto timeKeeper = std::make_shared<MockTimeKeeper>();
        const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
        (void)server.Mobilize(dep);
        auto connection = std::make_shared<MockConnection>();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET http://www.example.com/hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: " +
            testVector.hostUri +
            "\r\n"
            "Accept-Language: en, mi\r\n"
            "\r\n");
        connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
        Http::Client client;
        const auto response = client.ParseResponse(
            std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
        ASSERT_FALSE(response == nullptr);
        if (testVector.badRequestStatusExpected)
        {
            EXPECT_EQ(400, response->statusCode) << "Fail for test vector index" << index;
        } else
        { EXPECT_NE(400, response->statusCode) << "Fail for test vector index" << index; }
        ASSERT_FALSE(connection->broken) << "Failed for test vector index" << index;
        server.Demobilize();
        index++;
    }
}

TEST_F(ServerTests, ServerTests_ServerSetContentLength_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    std::vector<Uri::Uri> requestsReceived;
    const auto resourceDelegate =
        [&requestsReceived](std::shared_ptr<Http::Server::Request> request,
                            std::shared_ptr<Http::Connection> connection,
                            const std::string& trailer)
    {
        const auto response = std::make_shared<Http::Client::Response>();
        response->statusCode = 200;
        response->status = "OK";
        response->headers.SetHeader("Content-Type", "test/plain");
        response->body = "Hello!";
        requestsReceived.push_back(request->target);
        return response;
    };
    const auto unregistrationDelegate = server.RegisterResource({"foo"}, resourceDelegate);
    const std::string request(
        "GET /foo/bar HTTP/1.1\r\n"
        "Host: www.exemple.com\r\n"
        "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    ASSERT_EQ("6", response->headers.GetHeaderValue("Content-Length"));
}

TEST_F(ServerTests, ClientSentRequestWithTooLargePayload) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    std::vector<Uri::Uri> requestsReceived;
    const std::string request(
        "GET /hello.txt HTTP/1.1\r\n"
        "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
        "Host: www.example.com\r\n"
        "Content-Length: "
        "100000000000000000000000000000000000000000000000000000000000000\r\n"
        "Accept-Language: en, mi\r\n"
        "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(413, response->statusCode);
    EXPECT_EQ("Payload Too Large", response->status);
    EXPECT_TRUE(connection->broken);
}

TEST_F(ServerTests, ServerTests_DefaultServerUri_Test) {
    ASSERT_EQ("", server.GetConfigurationItem("Host"));
    const std::vector<std::string> testVectors{"www.example.com", "bad.example.com"};
    size_t index = 0;
    for (const auto testVector : testVectors)
    {
        auto transport = std::make_shared<MockTransport>();
        auto timeKeeper = std::make_shared<MockTimeKeeper>();
        const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
        (void)server.Mobilize(dep);
        auto connection = std::make_shared<MockConnection>();
        transport->connectionDelegate(connection);
        const std::string request(
            "GET /hello.txt HTTP/1.1\r\n"
            "User-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\n"
            "Host: " +
            testVector +
            "\r\n"
            "Accept-Language: en, mi\r\n"
            "\r\n");
        connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
        Http::Client client;
        const auto response = client.ParseResponse(
            std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
        ASSERT_FALSE(response == nullptr);
        EXPECT_NE(400, response->statusCode) << "Failed for test viector index" << index;
        server.Demobilize();
        index++;
    }
}

TEST_F(ServerTests, ServerTests_RegisterResourceSubspaceDelegate__Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);

    const std::string request =
        ("GET /foo/bar HTTP/1.1\r\n"
         "Host: www.exemple.com\r\n"
         "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();

    std::vector<Uri::Uri> requestsResived;
    const auto resourceDelegate = [&requestsResived](std::shared_ptr<Http::Server::Request> request,
                                                     std::shared_ptr<Http::Connection> connection,
                                                     const std::string& trailer)
    {
        const auto response = std::make_shared<Http::Client::Response>();
        response->statusCode = 200;
        response->status = "OK";
        requestsResived.push_back(request->target);
        return response;
    };
    const auto unregistrationDelegate = server.RegisterResource({"foo"}, resourceDelegate);
    ASSERT_TRUE(requestsResived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));

    EXPECT_EQ(200, response->statusCode);
    ASSERT_EQ(1, requestsResived.size());
    ASSERT_EQ((std::vector<std::string>{"bar"}), requestsResived[0].GetPath());
    connection->dataReceived.clear();
    unregistrationDelegate();
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();
}

TEST_F(ServerTests, ServerTests_RegisterResourceWideServerDelegate__Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);

    const std::string request =
        ("GET /foo/bar HTTP/1.1\r\n"
         "Host: www.exemple.com\r\n"
         "\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();

    std::vector<Uri::Uri> requestsResived;
    const auto resourceDelegate = [&requestsResived](std::shared_ptr<Http::Server::Request> request,
                                                     std::shared_ptr<Http::Connection> connection,
                                                     const std::string& trailer)
    {
        const auto response = std::make_shared<Http::Client::Response>();
        response->statusCode = 200;
        response->status = "OK";
        requestsResived.push_back(request->target);
        return response;
    };
    const auto unregistrationDelegate = server.RegisterResource({}, resourceDelegate);
    ASSERT_TRUE(requestsResived.empty());
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));

    EXPECT_EQ(200, response->statusCode);
    ASSERT_EQ(1, requestsResived.size());
    ASSERT_EQ((std::vector<std::string>{"foo", "bar"}), requestsResived[0].GetPath());
    connection->dataReceived.clear();
    unregistrationDelegate();
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(404, response->statusCode);
    connection->dataReceived.clear();
}

TEST_F(ServerTests, ServerTests_DontAllowDoubleRegistration_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);

    // Register /foo/bar delegate.
    const auto foobar = [](std::shared_ptr<Http::Server::Request> request,
                           std::shared_ptr<Http::Connection> connection, const std::string& trailer)
    { return std::make_shared<Http::Client::Response>(); };
    const auto unregisterfoobar = server.RegisterResource({"foo", "bar"}, foobar);

    // Attempt to register another /foo/bar delegate.
    // This should not be allowed because /foo/bar resource already has a handler.
    const auto anotherfoobar = [](std::shared_ptr<Http::Server::Request> request,
                                  std::shared_ptr<Http::Connection> connection,
                                  const std::string& trailer)
    { return std::make_shared<Http::Client::Response>(); };
    const auto unregisteranotherfoobar = server.RegisterResource({"foo", "bar"}, anotherfoobar);

    ASSERT_EQ(unregisteranotherfoobar, nullptr);
}

TEST_F(ServerTests, ServerTests_DontAllowOverlappingSubspaces_Test) {
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);

    // Register /foo/bar delegate.
    const auto foobar = [](std::shared_ptr<Http::Server::Request> request,
                           std::shared_ptr<Http::Connection> connection, const std::string& trailer)
    { return std::make_shared<Http::Client::Response>(); };
    auto unregisterfoobar = server.RegisterResource({"foo", "bar"}, foobar);

    ASSERT_FALSE(unregisterfoobar == nullptr);
    // Attempt to register /foo delegate.
    // This should not be allowed because it would overlap the /foo/bar delegate.
    const auto foo = [](std::shared_ptr<Http::Server::Request> request,
                        std::shared_ptr<Http::Connection> connection, const std::string& trailer)
    { return std::make_shared<Http::Client::Response>(); };
    auto unregisterfoo = server.RegisterResource({"foo"}, foo);

    ASSERT_EQ(unregisterfoo, nullptr);

    // Unregister /foo/bar and register /foo

    unregisterfoobar();
    unregisterfoo = server.RegisterResource({"foo"}, foo);
    ASSERT_FALSE(unregisterfoo == nullptr);

    // Attempt to register /foo/bar again.
    // This should not be allowd because it would overlap the /foo delegate.

    unregisterfoobar = server.RegisterResource({"foo", "bar"}, foobar);
    ASSERT_TRUE(unregisterfoobar == nullptr);
}

TEST_F(ServerTests, ServerTests_RequestInactivityTimeOut_Test) {
    const auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    Http::Server::MobilizationDependencies dep;
    dep.port = 1234;
    dep.transport = transport;
    dep.timeKeeper = timeKeeper;
    server.SetConfigurationItem("Port", "1234");
    server.SetConfigurationItem("InactivityTimeout", "10.0");
    server.SetConfigurationItem("RequestTimeout", "1.0");
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    timeKeeper->currentTime = 1.001;
    ASSERT_FALSE(connection->AwaitBroken());
    const std::string request =
        ("GET /foo/bar HTTP/1.1\r\n"
         "Host: www.exemple.com\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    timeKeeper->currentTime = 0.999;
    ASSERT_FALSE(connection->AwaitResponse());
    connection->dataReceivedDelegate({'x'});
    timeKeeper->currentTime = 1.001;
    ASSERT_TRUE(connection->AwaitResponse());
    Http::Client client;
    auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    EXPECT_EQ(408, response->statusCode);
    EXPECT_EQ("Request Timeout", response->status);
    ASSERT_TRUE(connection->AwaitBroken());
    connection->dataReceived.clear();
    timeKeeper->currentTime = 1.001;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(connection->dataReceived.empty());
}

TEST_F(ServerTests, MobiliseWhenAlreadyMobilized) {
    Http::Server::MobilizationDependencies deps;
    deps.transport = std::make_shared<MockTransport>();
    deps.timeKeeper = std::make_shared<MockTimeKeeper>();
    deps.port = 1234;
    ASSERT_TRUE(server.Mobilize(deps));
    ASSERT_FALSE(server.Mobilize(deps));
}

TEST_F(ServerTests, ServerTests_UpgradedConnexion__Test) {
    // Setup the server
    auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    const Http::Server::MobilizationDependencies dep = {transport, 1234, timeKeeper};
    (void)server.Mobilize(dep);

    //
    bool requestReceived = false;
    std::shared_ptr<Http::Connection> upgradedConnection;
    std::string dataReceivedAfterUpgrading;
    const auto resourceDelegate =
        [&upgradedConnection, &requestReceived, &dataReceivedAfterUpgrading](
            std::shared_ptr<Http::Server::Request> request,
            std::shared_ptr<Http::Connection> connection, const std::string& trailer)
    {
        const auto response = std::make_shared<Http::Client::Response>();
        response->statusCode = 101;
        response->status = "Switching Protocols";
        response->headers.SetHeader("Connection", "upgrade");
        upgradedConnection = connection;
        requestReceived = true;
        dataReceivedAfterUpgrading = trailer;
        upgradedConnection->SetConnectionBrokenDelegate([](bool graceful) {});
        upgradedConnection->SetDataReceivedDelegate(
            [&dataReceivedAfterUpgrading](std::vector<uint8_t> data)
            { dataReceivedAfterUpgrading += std::string(data.begin(), data.end()); });
        return response;
    };
    const auto unregistrationDelegate = server.RegisterResource({"foo"}, resourceDelegate);
    // Start connection to the server
    auto connection = std::make_shared<MockConnection>();
    bool connectionDestroyed = false;
    connection->onDestruction = [&connectionDestroyed] { connectionDestroyed = true; };
    transport->connectionDelegate(connection);

    const std::string request =
        ("Get /foo/bar HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "\r\n"
         "Hello!\r\n");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    Http::Client client;
    const auto response = client.ParseResponse(
        std::string(connection->dataReceived.begin(), connection->dataReceived.end()));
    connection->dataReceived.clear();
    EXPECT_TRUE(requestReceived);
    EXPECT_EQ(101, response->statusCode);
    ASSERT_EQ(connection, upgradedConnection);
    EXPECT_EQ("Hello!\r\n", dataReceivedAfterUpgrading);
    dataReceivedAfterUpgrading.clear();

    // Send the request again. This time the request should not
    // be routed to the resource handler, because the server should
    // have relayed the connection to the resource handler
    requestReceived = false;
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    EXPECT_TRUE(connection->dataReceived.empty());
    EXPECT_FALSE(connection->broken);
    EXPECT_FALSE(requestReceived);
    EXPECT_EQ(request, dataReceivedAfterUpgrading);
    // Release the upgraded connection. That should be the last
    // reference to the connection, so expect it to have been destroyed.
    connection = nullptr;
    upgradedConnection = nullptr;
    ASSERT_TRUE(connectionDestroyed);
}

TEST_F(ServerTests, ServerTests_IdleTimeOut_Test) {
    const auto transport = std::make_shared<MockTransport>();
    auto timeKeeper = std::make_shared<MockTimeKeeper>();
    Http::Server::MobilizationDependencies dep;
    dep.port = 1234;
    dep.transport = transport;
    dep.timeKeeper = timeKeeper;
    server.SetConfigurationItem("Port", "1234");
    server.SetConfigurationItem("InactivityTimeout", "10.0");
    server.SetConfigurationItem("RequestTimeout", "1.0");
    server.SetConfigurationItem("IdleTimeout", "100.0");
    (void)server.Mobilize(dep);
    auto connection = std::make_shared<MockConnection>();
    transport->connectionDelegate(connection);
    timeKeeper->currentTime = 1.0009;
    ASSERT_FALSE(connection->AwaitBroken());
    const std::string request =
        ("GET /foo/bar HTTP/1.1\r\n"
         "Host: www.exemple.com\r\n"
         "\r\n  ");
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    ASSERT_TRUE(connection->AwaitResponse());
    connection->dataReceived.clear();
    timeKeeper->currentTime = 2.00;
    ASSERT_FALSE(connection->AwaitBroken());
    connection->dataReceivedDelegate(std::vector<uint8_t>(request.begin(), request.end()));
    ASSERT_TRUE(connection->AwaitResponse());
    timeKeeper->currentTime = 30.00;
    ASSERT_FALSE(connection->AwaitBroken());
    timeKeeper->currentTime = 102.9;
    ASSERT_TRUE(connection->AwaitBroken());
}
