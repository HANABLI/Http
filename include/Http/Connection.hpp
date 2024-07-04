#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

/**
 * @file Connection.hpp
 * 
 * This declares the Http::Connection class
 * 
 * © 2024 by Hatem Nabli
 */

#include <functional>
#include <memory>
#include <stdint.h>
#include <vector>

namespace Http {

    class Connection
    {
        // Types
    public:

        /**
         * This is the delegate used to deliver received data to the owner of 
         * this interface.
         * 
         * @param[in] data
         *      This is the data that was received from the remote peer.
         */
        typedef std::function< void(std::vector< uint8_t > data) > DataReceivedDelegate;

        /**
         * This delegate is used to notify the user that the connection
         * has been broken.
         */
        typedef std::function< void() > BrokenDelegate;

    public:
        Connection(/* args */);
        ~Connection();

        // Methods
    public:

        /**
         * This method return a string that identifies
         * the peer of this connection in the context of the transport.
         * 
         * @return
         *      A string that identifies the peer of this connection
         *      in the context of the connection is returned.
         */
        virtual std::string GetPeerId() = 0;

        /**
         * This method sets the delegate to call whenever data is received
         * from the remote peer.
         * 
         * @param[in] dataReceivedDelegate
         *      This is the delegate to call whenever data is received
         *      from the remote peer.
         */
        virtual void SetDataReceivedDelegate(DataReceivedDelegate dataReceivedDelegate) = 0;
        
        /**
         * This method sets the delegate to call whenever the connection
         * has been broken.
         * 
         * @param[in] dataReceivedDelegate
         *      This is the delegate to call whenever the connection
         *      has been broken.
         */
        virtual void SetConnectionBrokenDelegate(BrokenDelegate brokenDelegate) = 0;

        /**
         * This method sends the given data to the remote peer.
         * 
         * @param[in] data
         *      This is the data to send to the remote peer.
         */
        virtual void sendData(std::vector< uint8_t > data) = 0;

    private:
        /* data */
    };

    
}


#endif /* HTTP_CONNECTION_HPP */