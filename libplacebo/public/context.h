/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBPLACEBO_CONTEXT_H
#define LIBPLACEBO_CONTEXT_H

// Increased any time the API changes.
#define PL_API_VER 1

// The log level associated with a given log message.
enum pl_log_level {
    PL_LOG_NONE = 0,
    PL_LOG_FATAL,   // results in total loss of function
    PL_LOG_ERR,     // serious error, may result in impaired function
    PL_LOG_WARN,    // warning. potentially harmful
    PL_LOG_INFO,    // informational message
    PL_LOG_DEBUG,   // verbose debug message, informational
    PL_LOG_TRACE,   // very verbose trace of activity
    PL_LOG_ALL = PL_LOG_TRACE,
};

// Meta-object to serve as a global entrypoint for the purposes of resource
// allocation, logging, etc.
struct pl_context;

// Creates a new, blank pl_context. The argument must be given as PL_API_VER
// (this is used to detect ABI mismatch due to broken linking)
struct pl_context *pl_context_create(int api_ver);

// This implicitly destroys all objects allocated from this pl_context in
// additiona to the pl_context itself. The pointer is set to NULL afterwards.
void pl_context_destroy(struct pl_context **ctx);

// Associate a log callback with the context. All messages, informational
// or otherwise, will get redirected to this callback.
void pl_context_set_log_cb(struct pl_context *ctx, void *priv,
                           void (*fun)(void *priv, enum pl_log_level level,
                                       const char *msg));

// Set the maximum log level for which messages will be delivered to the log
// callback. Setting this to PL_LOG_ALL means all messages will be forwarded,
// but doing so indiscriminately can result in decreased performance as
// debugging code paths are enabled based on the configured log level.
void pl_context_set_log_level(struct pl_context *ctx, enum pl_log_level level);

#endif // LIBPLACEBO_CONTEXT_H
