ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = CHANGES LICENSE README.md radiance.conf.dist
SUBDIRS = src
dist-hook:
	touch ${distdir}/configure
	patch -p2 -d ${distdir} --no-backup-if-mismatch < ../dist.patch
