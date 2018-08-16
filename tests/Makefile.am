## Process this file with automake to produce Makefile.in

#############################
# Icecast integration tests #
#############################

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
    $(top_srcdir)/tap-driver.sh


#
# Script-based tests
#

TESTS = \
    startup.test \
    admin.test


#
# Extra files needed by tests
#

EXTRA_DIST = $(TESTS) \
    icecast.xml \
    on-connect.sh
