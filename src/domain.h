#ifndef DOMAIN_H
#define DOMAIN_H

#include <atomic>
#include "radiance.h"

class domain {
	public:
		std::string host;
		domain(std::string host);
};
#endif
