// Copyright 2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mod_spdy/apache/log_message_handler.h"

#include <limits>
#include <string>
#include "base/debug/debugger.h"
#include "base/debug/stack_trace.h"
#include "base/logging.h"
#include "httpd.h"

// When HAVE_SYSLOG is defined, apache http_log.h will include syslog.h, which
// #defined LOG_* as numbers. This conflicts with what we are using those here.
#undef HAVE_SYSLOG
#include "http_log.h"

// Make sure we don't attempt to use LOG macros here, since doing so
// would cause us to go into an infinite log loop.
#undef LOG
#define LOG USING_LOG_HERE_WOULD_CAUSE_INFINITE_RECURSION

namespace {

apr_pool_t* log_pool = NULL;

const int kMaxInt = std::numeric_limits<int>::max();
int log_level_cutoff = kMaxInt;

int GetApacheLogLevel(int severity) {
  switch (severity) {
    case logging::LOG_INFO:
      // Note: ap_log_perror only prints NOTICE and higher messages.
      // TODO(sligocki): Find some way to print these as INFO if we can.
      //return APLOG_INFO;
      return APLOG_NOTICE;
    case logging::LOG_WARNING:
      return APLOG_WARNING;
    case logging::LOG_ERROR:
      return APLOG_ERR;
    case logging::LOG_ERROR_REPORT:
      return APLOG_CRIT;
    case logging::LOG_FATAL:
      return APLOG_ALERT;
    default:  // For VLOG()s
      // TODO(sligocki): return APLOG_DEBUG;
      return APLOG_NOTICE;
  }
}

bool LogMessageHandler(int severity, const char* file, int line,
                       size_t message_start, const std::string& str) {
  const int this_log_level = GetApacheLogLevel(severity);

  std::string message = str;
  if (severity == logging::LOG_FATAL) {
    if (base::debug::BeingDebugged()) {
      base::debug::BreakDebugger();
    } else {
      base::debug::StackTrace trace;
      std::ostringstream stream;
      trace.OutputToStream(&stream);
      message.append(stream.str());
    }
  }

  // Trim the newline off the end of the message string.
  size_t last_msg_character_index = message.length() - 1;
  if (message[last_msg_character_index] == '\n') {
    message.resize(last_msg_character_index);
  }

  if (this_log_level <= log_level_cutoff || log_level_cutoff == kMaxInt) {
    ap_log_perror(APLOG_MARK, this_log_level, APR_SUCCESS, log_pool,
                  "%s", message.c_str());
  }

  if (severity == logging::LOG_FATAL) {
    // Crash the process to generate a dump.
    base::debug::BreakDebugger();
  }

  return true;
}

// Include PID and TID in each log message.
bool kShowProcessId = true;
bool kShowThreadId = true;

// Disabled since this information is already included in the apache
// log line.
bool kShowTimestamp = false;

// Disabled by default due to CPU cost. Enable to see high-resolution
// timestamps in the logs.
bool kShowTickcount = false;

}  // namespace

namespace mod_spdy {

void InstallLogMessageHandler(apr_pool_t* pool) {
  log_pool = pool;
  logging::SetLogItems(kShowProcessId,
                       kShowThreadId,
                       kShowTimestamp,
                       kShowTickcount);
  logging::SetLogMessageHandler(&LogMessageHandler);
}

void SetLoggingLevel(int apache_log_level, int vlog_level) {
  switch (apache_log_level) {
    case APLOG_EMERG:
    case APLOG_ALERT:
      logging::SetMinLogLevel(logging::LOG_FATAL);
      break;
    case APLOG_CRIT:
      logging::SetMinLogLevel(logging::LOG_ERROR_REPORT);
      break;
    case APLOG_ERR:
      logging::SetMinLogLevel(logging::LOG_ERROR);
      break;
    case APLOG_WARNING:
      logging::SetMinLogLevel(logging::LOG_WARNING);
      break;
    case APLOG_NOTICE:
    case APLOG_INFO:
    case APLOG_DEBUG:
    default:
      logging::SetMinLogLevel(std::min(logging::LOG_INFO, -vlog_level));
      break;
  }
}

}  // namespace mod_spdy