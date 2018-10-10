sbin_PROGRAMS = radiance
radiance_SOURCES = ../config.h config.cpp config.h logger.h logger.cpp database.cpp database.h events.cpp events.h misc_functions.cpp \
	misc_functions.h radiance.cpp radiance.h report.cpp report.h response.cpp response.h domain.h debug.h debug.cpp\
	domain.cpp schedule.cpp schedule.h site_comm.cpp site_comm.h user.cpp user.h worker.cpp worker.h

AM_CXXFLAGS = -std=c++11 -march=native -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -Wall -Wfatal-errors $(PTHREAD_CFLAGS) $(BOOST_CPPFLAGS)
radiance_LDADD = $(PTHREAD_LIBS) $(BOOST_IOSTREAMS_LIB) $(BOOST_SYSTEM_LIB) $(BOOST_LOG_LIB) $(BOOST_LOG_SETUP_LIB) $(BOOST_DATE_TIME_LIB) $(BOOST_THREAD_LIB)
AM_LDFLAGS = -rdynamic -Wl,-O1 -Wl,--as-needed
