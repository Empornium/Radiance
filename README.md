# Radiance

Radiance is a BitTorrent tracker written in C++ for the [Luminance](http://www.github.com/Empornium/Luminance) project. It supports requests over TCP and can track both IPv4 and IPv6 peers in a dual-stack mixed swarm.

## Radiance Compile-time Dependencies

* [GCC/G++](http://gcc.gnu.org/) (4.7+ required; 4.8.1+ recommended)
* [LLVM/clang++](https://clang.llvm.org) (3.3+ required; 3.4+ recommended) (alternative to GCC/G++)
* [Boost](http://www.boost.org/) (1.55.0+ required)
* [libev](http://software.schmorp.de/pkg/libev.html) (required)
* [MySQL++](http://tangentsoft.net/mysql++/) (3.2.0+ required)
* [jemalloc](http://jemalloc.net) (optional, but highly recommended - preferred over tcmalloc)
* [TCMalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html) (optional)


### Standalone Installation

* Create the following tables:
 - `options`
 - `torrents`
 - `users_freeleeches`
 - `users_slots`
 - `users_main`
 - `xbt_client_blacklist`
 - `xbt_files_users`
 - `xbt_peers_history`
 - `xbt_snatched`

* Edit `radiance.conf` to your liking.

* Build Radiance:
```
autoreconf -i
./configure
make
sudo make install
```

# Configure options:
`--with-jemalloc` is recommended
`--with-tcmalloc` is a good alternative to jemalloc
`--enable-debug` can help to find the source of crashes


## Running Radiance

### Run-time options:

* `-c <path/to/radiance.conf>` - Path to config file. If unspecified, the current working directory is used.
* `-d` - Fork to the background and run as a service daemon.
* `-v` - Print version string and exit.

### Signals

* `SIGHUP` - Reload config
* `SIGUSR1` - Reload torrent list, user list and client blacklist
