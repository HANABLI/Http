#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

/**
 * @file Client.hpp
 * 
 * This module declares the Http::Client class
 * 
 * © 2024 by Hatem Nabli
 */

#include <memory>
#include <string>
#include <MessageHeaders/MessageHeaders.hpp>

namespace Http {

    /**
     * This class is used to generate HTTP Responses (for web servers)
     * and parse HTTP responses received back from web servers.
     */
    class Client {

    public:
        struct Response
        {
            /**
             * This is the machine-readable number that describes
             * the overall status of the Response
             */
            unsigned int statusCode;

            /**
             * This is the human-readable text that describes
             * the overall status of the Response.
             */
            std::string status;

            /**
             * This are the messages headers that were included 
             * in the Response
             */
            MessageHeaders::MessageHeaders headers;

            /**
             * This is the body of the Response, if there is a body.
             */
            std::string body;

            //Methods

            /**
             * This method generates the data to transmit to the client
             * to return this response to the client 
             * 
             * @return
             *      The data to transmit to the client to return this 
             *      response to the client is returned 
             */
            std::string GenerateToString() const;

        };
        
    public:
        ~Client();
        Client(const Client&) = delete; // Copy Constructor that creates a new object by making a copy of an existing object. 
        //It ensures that a deep copy is performed if the object contains dynamically allocated resources 
        Client(Client&&); // Move Constructor that transfers resources from an expiring object to a newly constructed object.
        Client& operator=(const Client&) = delete; //Copy Assignment Operation That assigns the values of one object to another object using the assignment operator (=)
        Client& operator=(Client&&); //Move Assignment Operator: Amove assignment operator efficiently transfers resources from one object to another.

    public:
        /**
        * This is the default constructor
        */
        Client();

        /**
         * This method parces the given string as a raw Http Response message.
         * If the string parses correctly, the equivalent Response is returned.
         * otherwise, nullptr is returned.
         * 
         * @param[in] rawResponse
         *      This is the raw HTTP Response message as a string.
         * 
         * @return
         *      The Response object is returned from the given raw 
         *      Http Response string.
         * 
         * @retval nullptr
         *      This is returned if the given rawResponse did not parse correctly.
         */
        std::shared_ptr< Response > ParseResponse(const std::string& rawResponse);

        /**
         * This method parces the given string as a raw Http Response message.
         * If the string parses correctly, the equivalent Response is returned.
         * otherwise, nullptr is returned.
         * 
         * @param[in] rawResponse
         *      This is the raw HTTP Response message as a string.
         * 
         * @param[out] messageEnd
         *      This is the raw HTTP Response message end
         * 
         * @return
         *      The Response object is returned from the given raw 
         *      Http Response string.
         * 
         * @retval nullptr
         *      This is returned if the given rawResponse did not parse correctly.
         */
        std::shared_ptr< Response > ParseResponse(const std::string& rawResponse, size_t& messageEnd);

        /**
         * 
         */


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

#endif /* HTTP_CLIENT_HPP */