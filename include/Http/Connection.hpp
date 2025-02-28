#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

/**
 * @file Connection.hpp
 *
 * This declares the Http::Connection class
 *
 * © 2024 by Hatem Nabli
 */

#include <stdint.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Http
{
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
        typedef std::function<void(const std::vector<uint8_t>& data)> DataReceivedDelegate;

        /**
         * This delegate is used to notify the user that the connection
         * has been broken.
         */
        typedef std::function<void(bool graceful)> BrokenDelegate;

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
        virtual void SendData(const std::vector<uint8_t>& data) = 0;

        /**
         * This method break the connection to the remote peer.
         *
         * @param[in] clean
         *      This flag idicates whenever or not to attempt to complete
         *      any data transmission still in progress, before breaking
         *      the connection.
         */
        virtual void Break(bool clean) = 0;
    };

}  // namespace Http

#endif /* HTTP_CONNECTION_HPP */