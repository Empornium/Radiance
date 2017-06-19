#ifndef RESPONSE_H
#define RESPONSE_H

#include <string>
#include "radiance.h"

const std::string response(const std::string &body, client_opts_t &client_opts, uint16_t response);
const std::string response_head(size_t content_length, client_opts_t &client_opts, uint16_t response);
const std::string get_reason(uint16_t response);
const std::string response_error(const std::string &err, client_opts_t &client_opts);
const std::string response_warning(const std::string &msg);

#endif
