#ifndef USER_H
#define USER_H

#include <atomic>
#include "radiance.h"

class user {
	private:
		userid_t id;
		bool deleted;
		bool leechstatus;
		bool protect_ip;
		bool ipv6;
		time_t personalfreeleech;
		time_t personaldoubleseed;
		struct {
			std::atomic<uint32_t> leeching;
			std::atomic<uint32_t> seeding;
		} stats;
	public:
		user(userid_t uid, bool leech, bool protect, bool track_ipv6, time_t pfl, time_t pds);
		const inline userid_t get_id() { return id; }
		const inline bool is_deleted() { return deleted; }
		void inline set_deleted(bool status) { deleted = status; }
		const bool inline is_protected() { return protect_ip; }
		void inline set_protected(bool status) { protect_ip = status; }
		const inline bool track_ipv6() { return ipv6; }
		void inline set_track_ipv6(bool status) { ipv6 = status; }
		const inline bool can_leech() { return leechstatus; }
		void inline set_leechstatus(bool status) { leechstatus = status; }
		void inline decr_leeching() { --stats.leeching; }
		void inline decr_seeding() { --stats.seeding; }
		void inline incr_leeching() { ++stats.leeching; }
		void inline incr_seeding() { ++stats.seeding; }
		void inline reset_stats() { stats.seeding=0; stats.leeching=0;}
		const inline uint32_t get_leeching() { return stats.leeching; }
		const inline uint32_t get_seeding() { return stats.seeding; }
		const inline time_t pfl () { return personalfreeleech; }
		const inline time_t pds () { return personaldoubleseed; }
		void inline set_personalfreeleech(time_t pfl) { personalfreeleech = pfl; }
		void inline set_personaldoubleseed(time_t pds) { personaldoubleseed = pds; }
};
#endif
