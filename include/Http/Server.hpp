#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

/**
 * @file Server.hpp
 * 
 * This module declares the Http::Server class
 * 
 * © 2024 by Hatem Nabli
 */

#include<memory>

namespace Http {

    class Server {
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