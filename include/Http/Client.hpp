#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

/**
 * @file Client.hpp
 * 
 * This module declares the Http::Client class
 * 
 * © 2024 by Hatem Nabli
 */

#include<memory>

namespace Http {

    class Client {
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

#endif /* HTTP_Client_HPP */