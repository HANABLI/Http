#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

/**
 * @file Server.hpp
 * 
 * This module declares the Http::Server class
 * 
 * © 2024 by Hatem Nabli
 */

#include "ServerTransportLayer.hpp"

#include <memory>
#include <ostream>
#include <stdint.h>
#include <string>
#include <Uri/Uri.hpp>
#include <SystemUtils/DiagnosticsSender.hpp>
#include <MessageHeaders/MessageHeaders.hpp>
namespace Http {

    class Server {

        public:
        /**
         * This is represents a HTTP server request structure, decomposed
         * into its various elements.
         */
        struct Request
        {
            // Types

            /**
             * This type is used to track how much of the next request
             * has been parsed so far.
             */
            enum class RequestParsingState {
                /**
                 * In this state, we're still waiting to receive
                 * the full request line.
                 */
                RequestLine,

                /**
                 * In this state, we're received and parsed the request
                 * line, and possibly some header lines, but haven't yet
                 * received all of the header lines.
                 */
                Headers,

                /**
                 * in this state, we've received and parsed the request
                 * line and headers, and possibly some of the body, but
                 * haven't yet received all of the body.
                 */
                Body,
                
                /**
                 * In this state, the request is fully constructed
                 * or is invalid, but the connection from which the request
                 * was constructed can remain open to accept another request.
                 */
                Complete, ///< request parsed successfully

                /**
                 * In this state, connection from which the request was
                 * constructed should be closed, either for security
                 * reasons, or because it would be impossible or unlikely
                 * to receive a valid request after this one.
                 */
                Error ///< bad request, server should close connection

            };

            /**
             * This flag indicates whether or not the request 
             * has passed all validity steps.
             */
            bool valid = true;

            /**
             * This is the request method to be performed on the
             * target resource.
             */
            std::string method;

            /**
             * This identifies the target resource upon which to apply
             * the request.
             */
            Uri::Uri target;

            /**
             * This are the messages headers that were included 
             * in the request
             */
            MessageHeaders::MessageHeaders headers;

            /**
             * This is the body of the request, if there is a body.
             */
            std::string body;

            /**
             * This flag indicates whether or not the request
             * was parced correctly. connection can still be used in some
             * invalidity cases.
             */
            RequestParsingState state = RequestParsingState::RequestLine;

            // Methods
            /**
             * This method returns an indication of whether or not the request
             * has been fully constructed (valid or not).
             * 
             * @return
             *      An indication of whether or not the request
             *      has been fully constructed is returned.
             */
            bool IsProcessed() const;
        };
        

        // LifeCycle managment
    public:
        ~Server();
        Server(const Server&) = delete; // Copy Constructor that creates a new object by making a copy of an existing object. 
        //It ensures that a deep copy is performed if the object contains dynamically allocated resources 
        Server(Server&&); // Move Constructor that transfers resources from an expiring object to a newly constructed object.
        Server& operator=(const Server&) = delete; //Copy Assignment Operation That assigns the values of one object to another object using the assignment operator (=)
        Server& operator=(Server&&); //Move Assignment Operator: Amove assignment operator efficiently transfers resources from one object to another.

    public:
        /**
        * This is the default constructor
        */
        Server();

        /**
         * This method returns the value of a given server configuration
         * item.
         * 
         * @param[in] key
         *      this is the key identifier of the configuration item
         *      whose value should be returned.
         * 
         * @return
         *      The value of the configuration item is returned.
         */
        std::string GetConfigurationItem(const std::string& key);
        
        /**
         * This method sets the value of the given server configuration 
         * item.
         * 
         * 
         * @param[in] key
         *      This is the key identifier of the configuration server
         *      item whose value should be set.
         * @param[in] value
         *      This is the value to be set for the configuration item
         */
        void SetConfigurationItem(const std::string& key, const std::string& value);

        /**
        * This method forms a new subscription to diagnostic
        * messages published by the sender.
        * 
        * @param[in] delegate
        *       This is the function to call to deliver messages
        *       to this subscriber.
        * 
        * @param[in] minLevel
        *       This is the minimum level of message that this subscriber
        *       desires to receive.
        * @return
        *       A function is returned which my be called
        *       to terminate the subscription.
        */
        SystemUtils::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemUtils::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        );

        /**
         * This method will cause the server to bind to the given transport 
         * layer and start accepting and processing connections from clients.
         * 
         * @param[in] transport
         *      This is the transport layer implementation to use.
         * 
         * @param[in] port
         *      This is the public port number to which clients may connect
         *      to establish connections with the server.
         */
        bool Mobilize(
            std::shared_ptr< ServerTransportLayer > transport,
            uint16_t port
        );

        /**
         * This method stops any accepting or processing of client connections,
         * and releases the transport layer, returning the server back to the
         * state it was in before Mobilize was called.
         */
        void Demobilize();

        /**
         * This method parses the given string as a raw Http request message.
         * If the string parses correctly, the equivalent Request is returned.
         * otherwise, nullptr is returned.
         * 
         * @param[in] rawRequest
         *      This is the raw HTTP request message as a string.
         * 
         * @return
         *      The Request object is returned from the given raw 
         *      Http request string.
         * 
         * @retval nullptr
         *      This is returned if the given rawRequest did not parse correctly.
         */
        std::shared_ptr< Request > ParseRequest(const std::string& rawRequest);

        /**
         * This method parses the given string as a raw Http request message.
         * If the string parses correctly, the equivalent Request is returned.
         * otherwise, nullptr is returned.
         * 
         * @param[in] rawRequest
         *      This is the raw HTTP request message as a string.
         * 
         * @param[out] messageEnd
         *      This is the raw HTTP request message as a single string.
         * 
         * @return
         *      The Request object is returned from the given raw 
         *      Http request string.
         * 
         * @retval nullptr
         *      This is returned if the given rawRequest is incomplete or did not parse correctly.
         */
        std::shared_ptr< Request > ParseRequest(const std::string& rawRequest, size_t& messageEnd);



    private:
        /* data */

        /**
         * This is the type of structure that contains the private
         * properties of the instance. It is defined in the implementation
         * and declared here to ensure that iwt is scoped inside the class.
        */
        struct Impl;

        /**
        * This contains the private properties of the instance.
        */       
        std::unique_ptr<struct Impl> impl_;
    };

    /**
     * This is a support function for googleTest to print out
     * values of the Server::Request::Validity class.
     * 
     * @param[in] state
     *      This is the request state to print out.
     * 
     * @param[in] os
     *      This is a pointer to the stream to wish to print 
     *      the request state.
     */
    void PrintTo(
        const Server::Request::RequestParsingState& state,
        std::ostream* os
    );
}

#endif /* HTTP_SERVER_HPP */