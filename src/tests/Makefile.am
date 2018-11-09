## Process this file with automake to produce Makefile.in

######################
# Icecast unit tests #
######################

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
    $(top_srcdir)/tap-driver.sh

check_PROGRAMS = 

#
# Helper library for TAP tests
#

libice_ctest_la_SOURCES = %reldir%/ctest_lib.c %reldir%/ctest_lib.h
noinst_LTLIBRARIES = libice_ctest.la

#
# Test programs
#

ctest_suite_test_SOURCES = %reldir%/ctest_suite.c
ctest_suite_test_LDADD = libice_ctest.la
check_PROGRAMS += ctest_suite.test

ctest_resourcematch_test_SOURCES = %reldir%/ctest_resourcematch.c
ctest_resourcematch_test_LDADD = libice_ctest.la icecast-resourcematch.o
check_PROGRAMS += ctest_resourcematch.test

# Add all programs to TESTS
TESTS = $(check_PROGRAMS)
