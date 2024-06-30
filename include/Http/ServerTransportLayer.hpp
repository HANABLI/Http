#ifndef HTTP_SERVER_TRANSPORT_LAYER_HPP
#define HTTP_SERVER_TRANSPORT_LAYER_HPP

/**
 * @file ServerTransportLayer.hpp
 * 
 * This module declares the Http::ServerTransportLayer interface.
 * 
 * © 2024 by Hatem Nabli
 */

#include "Connection.hpp"

#include <memory>
#include <functional>
#include <stdint.h>

namespace Http
{
    /**
     * This represents the transport layer requirements of the 
     * Server.To integrate Http::Server into a larger application,
     * implement this interfaces in terms of the actual transport layer.
     * 
     */
    class ServerTransportLayer
    {
        // Types
    public:
        
        /**
         * This delegate is used to notify the user that
         * new connection has been established for the server.
         * 
         * @param[in] connection
         *      This is the new connection that has been established for the server.
         */
        typedef std::function< void(std::shared_ptr< Connection > connection) > NewConnectionDelegate;


        // Methods
    public:

        /**
         * This method acquires exclusive access to the given port
         * on all network interface, and begins the process of listening
         * for and accepting incoming connection from clients.
         * 
         * @param[in] port
         *      This is the port number to which clients may
         *       establish connections with this server.
         * 
         * @param[in] newConnectionDelegate
         *      This is the delegate to call whenever a new connection
         *      has been established for the server.
         * 
         * @return
         *      An indication of whether or not the process begins listening
         *      to the given port.
         */
        virtual bool BindNetwork(uint16_t port, NewConnectionDelegate newConnectionDelegate) = 0;

        /**
         * This method releases all resources and access that were acquired
         * as a result of calling the BindNetwork method.
         */
        virtual void ReleaseNetwork() = 0;
    };
    

    

} // namespace Http


#endif /* HTPP_SERVER_TRANSPORT_LAYER_HPP */