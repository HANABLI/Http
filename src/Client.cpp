/**
 * @file Client.cpp
 * 
 * This module is an implementation of the
 * Http::Client class.
 * 
 * © 2024 by Hatem Nabli
 */

#include <Http/Client.hpp>

namespace Http {

    /**
     * This contains the private properties of a Client instance
     */

    struct Client::Impl
    {
        
    };

    Client::~Client() = default;

    Client::Client()
         : impl_(new Impl) {

    }
    
}