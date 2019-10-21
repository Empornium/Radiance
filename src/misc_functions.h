#ifndef MISC_FUNCTIONS__H
#define MISC_FUNCTIONS__H
#include <string>
#include <vector>

int lockRegion(int fd, int type, int whence, int start, int len);
int32_t strtoint32(const std::string& str);
int64_t strtoint64(const std::string& str);
std::string inttostr(int i);
std::string hex_decode(const std::string &in);
std::string bintohex(const std::string &in);
std::string trim(const std::string &str);
std::vector<std::string> split(const std::string &s, char delim);

#endif
