## Process this file with automake to produce Makefile.in

######################
# Icecast unit tests #
######################

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
    $(top_srcdir)/tap-driver.sh

check_PROGRAMS = 

#
# Test programs
#

ctest_suite_test_SOURCES = tests/ctest_suite.c
check_PROGRAMS += ctest_suite.test

ctest_resourcematch_test_SOURCES = tests/ctest_resourcematch.c
ctest_resourcematch_test_LDADD = icecast-resourcematch.o
check_PROGRAMS += ctest_resourcematch.test

ctest_refobject_test_SOURCES = tests/ctest_refobject.c
ctest_refobject_test_LDADD = \
    common/thread/libicethread.la \
    common/avl/libiceavl.la \
    icecast-refobject.o
check_PROGRAMS += ctest_refobject.test

ctest_buffer_test_SOURCES = tests/ctest_buffer.c
ctest_buffer_test_LDADD = \
    common/thread/libicethread.la \
    common/avl/libiceavl.la \
    icecast-refobject.o \
    icecast-buffer.o
check_PROGRAMS += ctest_buffer.test

# Add all programs to TESTS
TESTS = $(check_PROGRAMS)
