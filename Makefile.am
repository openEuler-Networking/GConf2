
SUBDIRS = gconf backends po doc examples
DIST_SUBDIRS=tests $(SUBDIRS)

EXTRA_DIST=gconf.m4.in gconf.spec.in gconf.spec TODO \
	   gconf-2.0.pc.in gconfd.service.in

install-data-local:
	-mkdir -p $(DESTDIR)$(datadir)/aclocal
	$(INSTALL_DATA) gconf.m4 $(DESTDIR)$(datadir)/aclocal/gconf-$(MAJOR_VERSION).m4
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/gconf.xml.defaults
	-chmod 755 $(DESTDIR)$(sysconfdir)/gconf/gconf.xml.defaults
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/gconf.xml.mandatory
	-chmod 755 $(DESTDIR)$(sysconfdir)/gconf/gconf.xml.mandatory

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gconf-2.0.pc

servicedir = $(datadir)/dbus/services

service_DATA = gconfd.service

gconfd.service: gconfd.service.in
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@

CLEANFILES = gconfd.service

install-schemas:
	(cd standard-schemas && $(MAKE) $(AM_MAKEFLAGS) install-schemas)

