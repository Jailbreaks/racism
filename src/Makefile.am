AM_CFLAGS = \
	$(GLOBAL_CFLAGS) \
	$(libimobiledevice_CFLAGS) \
	$(libplist_CFLAGS) \
	$(libzip_CFLAGS)

AM_LDFLAGS = \
	$(GLOBAL_LDFLAGS) \
	$(libimobiledevice_LIBS) \
	$(libplist_LIBS) \
	$(libzip_LIBS)

bin_PROGRAMS = racism

racism_SOURCES = racism.c
racism_CFLAGS = $(AM_CFLAGS)
racism_LDFLAGS = $(AM_LDFLAGS)

