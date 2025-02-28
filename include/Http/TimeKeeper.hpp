#ifndef HTTP_TIMEKEEPER_HPP
#define HTTP_TIMEKEEPER_HPP
/**
 * @file TimeKeeper.hpp
 *
 * This module declares the TimeKeeper interface.
 *
 * © 2024 by Hatem Nabli
 */

#include <memory>

namespace Http
{
    /**
     * This represents the time-keeping requirements of Http::Server.
     * To integrate Http::Server into a large program, implement this
     * interface in terms of the actual server time.
     *
     */
    class TimeKeeper
    {
        // Methods
    public:
        /**
         * This method returns the current server time.
         *
         * @return
         *      The current server time is returned in seconds.
         */
        virtual double GetCurrentTime() = 0;
    };
}  // namespace Http

#endif /* HTTP_TIMEKEEPER_HPP */
