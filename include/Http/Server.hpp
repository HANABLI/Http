#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

/**
 * @file Server.hpp
 * 
 * This module declares the Http::Server class
 * 
 * © 2024 by Hatem Nabli
 */
#include "IServer.hpp"
#include "ServerTransportLayer.hpp"

#include <functional>
#include <memory>
#include <ostream>
#include <stdint.h>
#include <string>
#include <Uri/Uri.hpp>
#include <Http/Client.hpp>
#include <SystemUtils/DiagnosticsSender.hpp>
#include <MessageHeaders/MessageHeaders.hpp>
namespace Http {

    class Server: public IServer {

    public:
        struct MobilizationDependencies
        {
            /* data */
            /**
             * This is the transport layer implementation.
             */
            std::shared_ptr< ServerTransportLayer > transport;
            /**
             * This represents the port number to wich clients may connect
             * to establish connections with the server.
             */
            uint16_t port;
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
         * This method will cause the server to bind to the given transport 
         * layer and start accepting and processing connections from clients.
         * 
         * @param[in] dependencies
         *      These are all of the configuration items and dependencies
         *      needed by the server to be mobilized.
         * @return
         *      
         */
        bool Mobilize(const MobilizationDependencies& dependencies);

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
        virtual std::string GetConfigurationItem(const std::string& key) override;
        
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
        virtual void SetConfigurationItem(const std::string& key, const std::string& value) override;

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
        virtual SystemUtils::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemUtils::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        ) override;

        /**
         * This method registers the given delegate to be called in order to generate
         * a response for any request that comes in to the server with a target URI which
         * identifies a resource within the given resource subspace of the server.
         * 
         * @param[in] resourceSubspacePath
         *      This identifies the subspace of resources that we want the given
         *      delegate to be responsible for handling.
         * 
         * @param[in] resourceDelegate
         *      This is the function to call in order to apply the given
         *      request and come up with a response when the request identifies 
         *      a resource within the given resource subspace of the server.
         * @return
         *      A function is returned which, if called, revokes the registration
         *      of the resource delegate, so that subsequent requests to any resource
         *      within the registered resource substate are no longer handled by the 
         *      formerly-registered delegate.
         */
        virtual UnregistrationDelegate RegisterResource(
            const std::vector< std::string >& resourceSubspacePath, 
            ResourceDelegate resourceDelegate
        ) override;
        
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
}

#endif /* HTTP_SERVER_HPP */