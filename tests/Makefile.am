## Process this file with automake to produce Makefile.in

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

TESTS = \
	startup.test \
	admin.test

EXTRA_DIST = $(TESTS)

EXTRA_DIST += \
	icecast.xml \
	on-connect.sh
