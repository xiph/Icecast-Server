## Process this file with automake to produce Makefile.in

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

TESTS = \
	startup.test \
	admin.test \
	ctest_suite.test \
	ctest_resourcematch.test \
	ctest_refobject.test

EXTRA_DIST = $(TESTS)

EXTRA_DIST += \
	icecast.xml \
	on-connect.sh

check_PROGRAMS = ctest_suite.test ctest_resourcematch.test ctest_refobject.test
noinst_HEADERS = ctest_lib.h

ctest_suite_test_SOURCES=ctest_suite.c ctest_lib.c
ctest_resourcematch_test_SOURCES=ctest_resourcematch.c ctest_lib.c ../src/resourcematch.c
ctest_refobject_test_SOURCES=ctest_refobject.c ctest_lib.c ../src/refobject.c
ctest_refobject_test_DEPENDENCIES = ../src/common/thread/libicethread.la ../src/common/avl/libiceavl.la
ctest_refobject_test_LDADD = $(ctest_refobject_test_DEPENDENCIES)
