
EFENCE=

INCLUDES = -I$(top_srcdir) -I$(top_builddir) \
	 $(DEPENDENT_CFLAGS) \
	 -DG_LOG_DOMAIN=\"GConf-Tests\" -DGCONF_ENABLE_INTERNALS=1

noinst_PROGRAMS=testgconf testlisteners testschemas testchangeset testencode testunique testpersistence testdirlist testaddress testbackend

TESTLIBS= $(INTLLIBS) $(DEPENDENT_LIBS) $(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la  $(EFENCE)

testunique_SOURCES=testunique.c

testunique_LDADD = $(TESTLIBS)

testgconf_SOURCES=testgconf.c dbus-glue.c dbus-glue.h

testgconf_LDADD = $(TESTLIBS)

testpersistence_SOURCES=testpersistence.c dbus-glue.c dbus-glue.h

testpersistence_LDADD = $(TESTLIBS)

testlisteners_SOURCES=testlisteners.c 

testlisteners_LDADD = $(TESTLIBS)

testschemas_SOURCES=testschemas.c dbus-glue.c dbus-glue.h

testschemas_LDADD = $(TESTLIBS)

testchangeset_SOURCES=testchangeset.c dbus-glue.c dbus-glue.h

testchangeset_LDADD = $(TESTLIBS)

testencode_SOURCES=testencode.c

testencode_LDADD = $(TESTLIBS)

testdirlist_SOURCES=testdirlist.c dbus-glue.c dbus-glue.h

testdirlist_LDADD = $(TESTLIBS)


testaddress_SOURCES=testaddress.c

testaddress_LDADD = $(TESTLIBS)

testbackend_SOURCES=testbackend.c

testbackend_LDADD = $(TESTLIBS)









