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

ctest_string_renderer_test_SOURCES = tests/ctest_string_renderer.c
ctest_string_renderer_test_LDADD = \
    icecast-util_string.o \
    icecast-string_renderer.o
check_PROGRAMS += ctest_string_renderer.test

ctest_crypt_test_SOURCES = tests/ctest_crypt.c
ctest_crypt_test_LDADD = \
    icecast-util_string.o \
    icecast-md5.o
check_PROGRAMS += ctest_crypt.test

# Add all programs to TESTS
TESTS = $(check_PROGRAMS)
