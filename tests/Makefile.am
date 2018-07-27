## Process this file with automake to produce Makefile.in

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

TESTS = \
	startup.test \
	admin.test \
	ctest_suite.test

EXTRA_DIST = $(TESTS)

EXTRA_DIST += \
	icecast.xml \
	on-connect.sh

check_PROGRAMS = ctest_suite.test
noinst_HEADERS = ctest_lib.h

ctest_suite_test_SOURCES=ctest_suite.c ctest_lib.c
