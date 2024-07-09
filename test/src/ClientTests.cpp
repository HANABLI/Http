/**
 * @file ClientTests.cpp
 * 
 * This module contains tests units of the 
 * Http::Client class.
 */

#include <gtest/gtest.h>
#include <Http/Client.hpp>
#include <Uri/Uri.hpp>

TEST(ClientTests, ClientTests_ParseGetResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 51\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello World! My payload includes a trailing CRLF.\r\n"
    );
    ASSERT_FALSE(response == nullptr);
    ASSERT_EQ(200, response->statusCode);
    ASSERT_TRUE(response->headers.HasHeader("Date"));
    ASSERT_EQ("Mon, 27 Jul 2009 12:28:53 GMT", response->headers.GetHeaderValue("Date"));
    ASSERT_TRUE(response->headers.HasHeader("Accept-Ranges"));
    ASSERT_EQ("bytes", response->headers.GetHeaderValue("Accept-Ranges"));
    ASSERT_TRUE(response->headers.HasHeader("Content-Type"));
    ASSERT_EQ("text/plain", response->headers.GetHeaderValue("Content-Type"));
    ASSERT_EQ("Hello World! My payload includes a trailing CRLF.\r\n", response->body);
}

TEST(ClientTests, ClientTests_ParseIncompleteBodyResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 55\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello World! My payload includes a trailing CRLF.\r\n"
    );
    ASSERT_TRUE(response == nullptr);
}

TEST(ClientTests, ClientTests_ParseIncompleteHEadersResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Content-Length: "
    );
    ASSERT_TRUE(response == nullptr);
}

TEST(ClientTests, ClientTests_ParseIncompleteStatusLineResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 \r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Content-Length: "
    );
    ASSERT_TRUE(response == nullptr);
}

TEST(ClientTests, ClientTests_ParseNoBodyDelimiterResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 51\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Type: text/plain\r\n"
    );
    ASSERT_TRUE(response == nullptr);
}

TEST(ClientTests, ClientTests_ParseNoContentLengthResponse_Test) {
    Http::Client client;
    const auto response = client.ParseResponse(
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
        "Server: Apache\r\n"
        "Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
        "ETag: \"34aa387-d-1568eb00\"\r\n"  
        "Accept-Ranges: bytes\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Type: text/plain\r\n"
         "\r\n"
        "Hello World! My payload includes a trailing CRLF.\r\n"
    );
    ASSERT_FALSE(response == nullptr);
    ASSERT_EQ("", response->body);
}