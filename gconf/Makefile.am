NULL=

GCONFD_BINARY_NAME=gconfd-2

INCLUDES=								\
	-I$(top_srcdir)							\
	-I$(top_builddir)						\
	$(DEPENDENT_WITH_XML_AND_GTK_CFLAGS) 				\
	$(GCONF_IPC_CFLAGS)						\
	-DG_LOG_DOMAIN=\"GConf\"					\
	-DGCONF_LOCALE_DIR=\""$(gconflocaledir)"\"			\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\"			\
	-DGCONF_CONFDIR=\""$(sysconfdir)/gconf/$(MAJOR_VERSION)"\"	\
	-DGCONF_ETCDIR=\""$(sysconfdir)/gconf"\"			\
	-DGCONF_BINDIR=\""$(bindir)"\"					\
	-DGCONF_SERVERDIR=\""$(libexecdir)"\"           		\
	-DGCONF_BUILDDIR=\""$(top_builddir)"\"				\
	-DGCONF_BACKEND_DIR=\""$(pkglibdir)/$(MAJOR_VERSION)"\"		\
	-DVERSION=\""$(VERSION)"\"					\
	-DGCONF_ENABLE_INTERNALS=1					\
	-DGCONFD=\""$(GCONFD_BINARY_NAME)"\"				\
	-DDBUS_API_SUBJECT_TO_CHANGE=\"1\"				\
	$(NULL)

EFENCE=

if GTK
SANITY_CHECK=gconf-sanity-check-2
else
SANITY_CHECK=
endif

lib_LTLIBRARIES = libgconf-2.la libgconf-dbus.la

bin_PROGRAMS = gconftool-2 gconftool-dbus
libexec_PROGRAMS = gconfd-2 $(SANITY_CHECK)

if HAVE_ORBIT
CORBA_GENERATED = GConfX-common.c GConfX-skels.c GConfX-stubs.c GConfX.h
else
CORBA_GENERATED =
endif

## not BUILT_SOURCES which seems to make a mess. fix with automake 1.5?
built_sourcecode = 			\
	$(CORBA_SOURCECODE)		\
	gconfmarshal.h			\
	gconfmarshal.c

$(libgconf_dbus_la_OBJECTS) $(libgconf_2_la_OBJECTS) $(gconftool_2_OBJECTS) $(gconfd_2_OBJECTS): $(built_sourcecode)

CLEANFILES = \
	$(built_sourcecode)	\
	stamp-gconfmarshal.h	\
	stamp-gconfmarshal.c	\
	stamp-gconfmarshal.h    \
	stamp-gconfmarshal.c

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

gconf_headers = 		\
	gconf.h			\
	gconf-changeset.h	\
	gconf-listeners.h	\
	gconf-schema.h		\
	gconf-value.h		\
	gconf-error.h		\
	gconf-engine.h		\
	gconf-client.h

gconfincludedir = $(includedir)/gconf/$(MAJOR_VERSION)/gconf
gconfinclude_HEADERS = 		\
	$(gconf_headers) 	\
	gconf-enum-types.h

# Only one client is supported at a time, if D-BUS is enabled we use that.
#if HAVE_DBUS
GCONF_DBUS_SOURCE = gconf-dbus.c gconf-dbus-utils.c gconf-dbus-utils.h
#else
#GCONF_DBUS_SOURCE = 
#endif

# Note: Separate these variables just to be able to build both dbus
# and corba clients for testing, remove this later to make sure we'll
# only build the dbus client when ipc = dbus or both.

if HAVE_ORBIT
GCONF_CORBA_SOURCE = gconf-corba-utils.c gconf-corba-utils.h gconf-corba.c
else
GCONF_CORBA_SOURCE =
endif

GCONF_IPC_SOURCE = $(GCONF_CORBA_SOURCE) $(GCONF_DBUS_SOURCE)

if HAVE_ORBIT
GCONFD_CORBA_SOURCE = gconf-database-corba.c gconf-database-corba.h \
	gconfd-corba.c gconfd-corba.h gconf-corba-utils.c gconf-corba-utils.h
else 
GCONFD_CORBA_SOURCE =
endif

if HAVE_DBUS
GCONFD_DBUS_SOURCE = gconfd-dbus.c gconfd-dbus.h gconf-database-dbus.c \
	gconf-database-dbus.h
else 
GCONFD_DBUS_SOURCE = 
endif

GCONFD_IPC_SOURCE = $(GCONFD_CORBA_SOURCE) $(GCONFD_DBUS_SOURCE)

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_2_SOURCES = \
	gconf-database.h	\
	gconf-database.c	\
	$(GCONFD_IPC_SOURCE)	\
	$(CORBA_GENERATED)      \
	gconf-sources.h		\
	gconfd.h		\
	gconfd.c

gconfd_2_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_LIBS) $(GCONF_IPC_LIBS) libgconf-$(MAJOR_VERSION).la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(OAF_LIBS) libgconf-client.la

gconftool_2_SOURCES = \
	gconftool.c

gconftool_2_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_WITH_XML_LIBS) $(GCONF_IPC_LIBS) $(POPT_LIBS) libgconf-$(MAJOR_VERSION).la

gconftool_dbus_SOURCES = \
	gconftool.c

gconftool_dbus_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_WITH_XML_LIBS) $(GCONF_IPC_LIBS) $(POPT_LIBS) libgconf-dbus.la

gconf_sanity_check_2_SOURCES = \
	gconf-sanity-check.c

gconf_sanity_check_2_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_WITH_XML_AND_GTK_LIBS) $(POPT_LIBS) $(GCONF_IPC_LIBS) libgconf-$(MAJOR_VERSION).la

$(CORBA_GENERATED): $(srcdir)/GConfX.idl $(ORBIT_IDL)
	$(ORBIT_IDL) $(srcdir)/GConfX.idl

GCONF_COMMON_SOURCE = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf.h			\
	gconf.c			\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf-changeset.c	\
	gconf-error.c		\
	gconf-listeners.c	\
	gconf-locale.h  	\
	gconf-locale.c  	\
	gconf-schema.c		\
	gconf-sources.c		\
	gconf-value.c		\
	gconf-client.c		\
	gconf-enum-types.c

libgconf_2_la_SOURCES = \
	$(GCONF_COMMON_SOURCE)	\
	$(GCONF_CORBA_SOURCE)	\
	$(CORBA_GENERATED)

libgconf_2_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE) -no-undefined
libgconf_2_la_LIBADD = $(INTLLIBS) $(DEPENDENT_LIBS) $(GCONF_IPC_LIBS)

libgconf_dbus_la_SOURCES = \
	$(GCONF_COMMON_SOURCE)	\
	$(GCONF_DBUS_SOURCE)

libgconf_dbus_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE) -no-undefined
libgconf_dbus_la_LIBADD = $(INTLLIBS) $(DEPENDENT_LIBS) $(GCONF_IPC_LIBS)

EXTRA_DIST=GConfX.idl default.path.in gconfmarshal.list regenerate-enum-header.sh regenerate-enum-footer.sh gconf-corba.c gconf-corba.h gconf-corba-utils.c gconf-corba.utils.h gconf-database-corba.c gconf-database-corba.h gconfd-corba.c gconf-dbus.c gconf-database-dbus.c gconf-database-dbus.h gconfd-dbus.c gconfd-dbus.h

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)
	$(INSTALL_DATA) default.path $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)/path

gconfmarshal.h: @REBUILD@ stamp-gconfmarshal.h
	@true

gconfmarshal.c: @REBUILD@ stamp-gconfmarshal.c
	@true

stamp-gconfmarshal.h: @REBUILD@ gconfmarshal.list
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --header >> xgen-gmh \
	&& (cmp -s xgen-gmh gconfmarshal.h || cp xgen-gmh gconfmarshal.h) \
	&& rm -f xgen-gmh xgen-gmh~

stamp-gconfmarshal.c: @REBUILD@ gconfmarshal.list
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --body >> xgen-gmc \
	&& (cmp -s xgen-gmc gconfmarshal.c || cp xgen-gmc gconfmarshal.c) \
	&& rm -f xgen-gmc xgen-gmc~

regenerate-built-sources:
	GCONF_SRCDIR=$(srcdir) $(srcdir)/regenerate-enum-header.sh $(gconf_headers)
	GCONF_SRCDIR=$(srcdir) $(srcdir)/regenerate-enum-footer.sh $(gconf_headers)

