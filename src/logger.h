#define BOOST_LOG_DYN_LINK 1

#ifndef RADIANCE_LOGGER_H
#define RADIANCE_LOGGER_H

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/named_scope.hpp>

enum severity_level
{
  trace,
  debug,
  info,
  warning,
  error,
  fatal
};

#define syslog(lvl) BOOST_LOG_TRIVIAL(lvl)

void init_log(void);

#endif
