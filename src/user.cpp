#include "user.h"

user::user(userid_t uid, bool leech, bool protect, bool track_ipv6, time_t pfl, time_t pds) : id(uid), deleted(false), leechstatus(leech), protect_ip(protect), ipv6(track_ipv6), personalfreeleech(pfl), personaldoubleseed(pds) {
	stats.leeching = 0;
	stats.seeding = 0;
}
