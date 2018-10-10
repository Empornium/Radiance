#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <sstream>
#include <fcntl.h>

#include "misc_functions.h"

static int lockReg(int fd, int cmd, int type, int whence, int start, off_t len) {
	struct flock fl;
	fl.l_type = type;
	fl.l_whence = whence;
	fl.l_start = start;
	fl.l_len = len;
	return fcntl(fd, cmd, &fl);
}

int lockRegion(int fd, int type, int whence, int start, int len) {
	return lockReg(fd, F_SETLK, type, whence, start, len);
}

int32_t strtoint32(const std::string& str) {
	std::istringstream stream(str);
	int32_t i = 0;
	stream >> i;
	return i;
}

int64_t strtoint64(const std::string& str) {
	std::istringstream stream(str);
	int64_t i = 0;
	stream >> i;
	return i;
}


std::string inttostr(const int i) {
	std::string str;
	std::stringstream out;
	out << i;
	str = out.str();
	return str;
}

std::string hex_decode(const std::string &in) {
	std::string out;
	out.reserve(20);
	unsigned int in_length = in.length();
	for (unsigned int i = 0; i < in_length; i++) {
		unsigned char x = '0';
		if (in[i] == '%' && (i + 2) < in_length) {
			i++;
			if (in[i] >= 'a' && in[i] <= 'f') {
				x = static_cast<unsigned char>((in[i]-87) << 4);
			} else if (in[i] >= 'A' && in[i] <= 'F') {
				x = static_cast<unsigned char>((in[i]-55) << 4);
			} else if (in[i] >= '0' && in[i] <= '9') {
				x = static_cast<unsigned char>((in[i]-48) << 4);
			}

			i++;
			if (in[i] >= 'a' && in[i] <= 'f') {
				x += static_cast<unsigned char>(in[i]-87);
			} else if (in[i] >= 'A' && in[i] <= 'F') {
				x += static_cast<unsigned char>(in[i]-55);
			} else if (in[i] >= '0' && in[i] <= '9') {
				x += static_cast<unsigned char>(in[i]-48);
			}
		} else {
			x = in[i];
		}
		out.push_back(x);
	}
	return out;
}

std::string bintohex(const std::string &in) {
	std::string out;
	size_t length = in.length();
	out.reserve(2*length);
	for (unsigned int i = 0; i < length; i++) {
		unsigned char x = static_cast<unsigned char>((in[i] & 0xF0) >> 4);
		if (x > 9) {
			x += 'a' - 10;
		} else {
			x += '0';
		}
		out.push_back(x);
		x = in[i] & 0x0F;
		if (x > 9) {
			x += 'a' - 10;
		} else {
			x += '0';
		}
		out.push_back(x);
	}
	return out;
}

std::string trim(const std::string &str) {
        size_t ltrim = str.find_first_not_of(" \t");
        if (ltrim == std::string::npos) {
                ltrim = 0;
        }
        size_t rtrim = str.find_last_not_of(" \t");
        if (ltrim != 0 || rtrim != str.length() - 1) {
                return str.substr(ltrim, rtrim - ltrim + 1);
        }
        return str;
}
