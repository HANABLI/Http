#ifndef HTTP_I_SERVER_HPP
#define HTTP_I_SERVER_HPP

/**
 * @file Server.hpp
 *
 * This module declares the Http::IServer interface.
 *
 * © 2024 by Hatem Nabli
 */

#include "ServerTransportLayer.hpp"

#include <stdint.h>
#include <Http/Client.hpp>
#include <MessageHeaders/MessageHeaders.hpp>
#include <SystemUtils/DiagnosticsSender.hpp>
#include <Uri/Uri.hpp>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
namespace Http
{
    /**
     * This is the public interface to the webserver from plugins
     * and other modules that ara outside the HTTP server.
     */
    class IServer
    {
        //
    public:
        /**
         * This is represents a HTTP server request structure, decomposed
         * into its various elements.
         */
        struct Request
        {
            // Types

            /**
             * This type is used to track how much of the next request
             * has been parsed so far.
             */
            enum class RequestParsingState
            {
                /**
                 * In this state, we're still waiting to receive
                 * the full request line.
                 */
                RequestLine,

                /**
                 * In this state, we're received and parsed the request
                 * line, and possibly some header lines, but haven't yet
                 * received all of the header lines.
                 */
                Headers,

                /**
                 * in this state, we've received and parsed the request
                 * line and headers, and possibly some of the body, but
                 * haven't yet received all of the body.
                 */
                Body,

                /**
                 * In this state, the request is fully constructed
                 * or is invalid, but the connection from which the request
                 * was constructed can remain open to accept another request.
                 */
                Complete,  ///< request parsed successfully

                /**
                 * In this state, connection from which the request was
                 * constructed should be closed, either for security
                 * reasons, or because it would be impossible or unlikely
                 * to receive a valid request after this one.
                 */
                Error  ///< bad request, server should close connection

            };

            /**
             * This flag indicates whether or not the request
             * has passed all validity steps.
             */
            bool valid = true;

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

            /**
             * This flag indicates whether or not the request
             * was parced correctly. connection can still be used in some
             * invalidity cases.
             */
            RequestParsingState state = RequestParsingState::RequestLine;

            /**
             * If the state of the request is State::Error, or if
             * the request is not valid, this indicates the status
             * code which should be given back to the client.
             */
            unsigned int responseStatusCode = 400;

            /**
             * If the state of the request is State::Error, or if
             * the request is not valid, this is the code description
             * of the satus code.
             */
            std::string responseStatusPhrase{"Bad Request"};

            // Methods
            /**
             * This method returns an indication of whether or not the request
             * has been fully constructed (valid or not).
             *
             * @return
             *      An indication of whether or not the request
             *      has been fully constructed is returned.
             */
            bool IsProcessed() const;
        };

        /**
         * This is the type of the function to be registred to handle
         * HTTP requests.
         *
         * @param[in] request
         *      This is the request to apply to the resource.
         *
         * @return
         *      The response to be returned to the client is returned.
         */
        typedef std::function<std::shared_ptr<Client::Response>(
            std::shared_ptr<Request> request, std::shared_ptr<Connection> connection,
            const std::string& trailer)>
            ResourceDelegate;

        /**
         * This is the type of function returned by RegisterResource,
         * to be called when the resource should be unregistered from
         * the server
         */
        typedef std::function<void()> UnregistrationDelegate;

    public:
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
        virtual std::string GetConfigurationItem(const std::string& key) = 0;

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
        virtual void SetConfigurationItem(const std::string& key, const std::string& value) = 0;

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
            size_t minLevel = 0) = 0;

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
            const std::vector<std::string>& resourceSubspacePath,
            ResourceDelegate resourceDelegate) = 0;
    };
    /**
     * This is a support function for googleTest to print out
     * values of the Server::Request::Validity class.
     *
     * @param[in] state
     *      This is the request state to print out.
     *
     * @param[in] os
     *      This is a pointer to the stream to wish to print
     *      the request state.
     */
    void PrintTo(const IServer::Request::RequestParsingState& state, std::ostream* os);
}  // namespace Http

#endif /* HTTP_I_SERVER_HPP */