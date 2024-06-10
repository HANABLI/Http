/**
 * @file Server.cpp
 * 
 * This module is an implementation of the
 * Http::Server class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <Http/Server.hpp>

namespace Http {

    /**
     * This contains the private properties of a Server instance
     */

    struct Server::Impl
    {
        
    };

    Server::~Server() = default;

    Server::Server()
         : impl_(new Impl) {

    }
    
}