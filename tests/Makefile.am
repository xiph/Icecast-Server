## Process this file with automake to produce Makefile.in

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
    $(top_srcdir)/tap-driver.sh

check_PROGRAMS =

#
# Helper library for TAP tests
#

libice_ctest_la_SOURCES = ctest_lib.c ctest_lib.h
noinst_LTLIBRARIES = libice_ctest.la


#
# Script-based tests
#

TESTS = \
    startup.test \
    admin.test


#
# Test programs
#

ctest_suite_test_SOURCES = ctest_suite.c
ctest_suite_test_LDADD = libice_ctest.la
check_PROGRAMS += ctest_suite_test

ctest_resourcematch_test_SOURCES = ctest_resourcematch.c
ctest_resourcematch_test_LDADD = libice_ctest.la ../src/icecast-resourcematch.o
check_PROGRAMS += ctest_resourcematch_test

ctest_refobject_test_SOURCES = ctest_refobject.c
ctest_refobject_test_LDADD = libice_ctest.la \
    ../src/common/thread/libicethread.la \
    ../src/common/avl/libiceavl.la \
    ../src/icecast-refobject.o
check_PROGRAMS += ctest_refobject_test


#
# Extra files needed by tests
#

EXTRA_DIST = $(TESTS) \
    icecast.xml \
    on-connect.sh

TESTS += $(check_PROGRAMS)
