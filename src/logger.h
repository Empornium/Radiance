#define BOOST_LOG_DYN_LINK 1

#ifndef RADIANCE_LOGGER_H
#define RADIANCE_LOGGER_H

#include <boost/log/trivial.hpp>

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
void rotate_log(void);
void flush_log(void);

#endif
