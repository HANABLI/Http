#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

/**
 * @file Server.hpp
 * 
 * This module declares the Http::Server class
 * 
 * © 2024 by Hatem Nabli
 */

#include <memory>
#include <string>
#include <Uri/Uri.hpp>
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
         * This method parces the given string as a raw Http request message.
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