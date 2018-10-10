#define BOOST_LOG_DYN_LINK 1

#include "../autoconf.h"
#include "radiance.h"
#include "logger.h"
#include "config.h"
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

//Fugley, but it works
boost::shared_ptr<
    boost::log::v2_mt_posix::sinks::synchronous_sink<
        boost::log::v2_mt_posix::sinks::text_file_backend
    >
> fsSink;

void init_log(void) {
  boost::log::core::get()->flush();
  boost::log::core::get()->remove_all_sinks();
  boost::log::core::get()->reset_filter();

  boost::log::add_common_attributes();
  boost::log::core::get()->add_global_attribute("Scope",
  boost::log::attributes::named_scope());

  auto severity = boost::log::trivial::info;
       if(conf->get_str("syslog_level") == "trace")    severity=boost::log::trivial::trace;
  else if(conf->get_str("syslog_level") == "debug")    severity=boost::log::trivial::debug;
  else if(conf->get_str("syslog_level") == "info")     severity=boost::log::trivial::info;
  else if(conf->get_str("syslog_level") == "warning")  severity=boost::log::trivial::warning;
  else if(conf->get_str("syslog_level") == "error")    severity=boost::log::trivial::error;
  else if(conf->get_str("syslog_level") == "fatal")    severity=boost::log::trivial::fatal;
  else if(conf->get_str("syslog_level") == "off") {
    boost::log::core::get()->set_logging_enabled(false);
    std::cout << "Logging disabled" << std::endl;
    return;
  }
  // Misconfigured
  else {
    std::cout << "Invalid log level: \"" << conf->get_str("syslog_level") << '"' << std::endl;
    exit(EXIT_FAILURE);
  }

  boost::log::core::get()->set_filter(
    boost::log::trivial::severity >= severity
  );
  auto fmtTimeStamp = boost::log::expressions::
    format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S");
  auto fmtSeverity = boost::log::expressions::
    attr<boost::log::trivial::severity_level>("Severity");

  // File log format.
  boost::log::formatter fileLogFmt =
    boost::log::expressions::format("[%1%] [%2%]\t%3%")
    % fmtTimeStamp % fmtSeverity % boost::log::expressions::smessage;

  // Console log format.
  boost::log::formatter consoleLogFmt =
    boost::log::expressions::format("[%1%] %2%")
    % fmtTimeStamp % boost::log::expressions::smessage;

  if(conf->get_str("syslog_path") != "off") {
    fsSink = boost::log::add_file_log(
        boost::log::keywords::file_name = conf->get_str("syslog_path"),
        boost::log::keywords::min_free_space = 30 * 1024 * 1024,
        boost::log::keywords::open_mode = std::ios_base::app
    );
    fsSink->set_formatter(fileLogFmt);

    #if defined(__DEBUG_BUILD__)
    fsSink->locked_backend()->auto_flush(true);
    #endif
  } else {
    auto consoleSink = boost::log::add_console_log(std::clog);
    consoleSink->set_formatter(consoleLogFmt);
  }
}

// Logrotate is hanging the tracker, annoying as it really shouldn't.
// Trying this as a work around.
void rotate_log(void) {
  auto oldLFS = fsSink;
  init_log();
  boost::log::core::get()->remove_sink(oldLFS);
}

void flush_log(void) {
  // Check if the log is open before flushing!
  if (fsSink) {
    fsSink->flush();
  }
}
