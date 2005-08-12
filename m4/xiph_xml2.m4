dnl XIPH_PATH_XML
dnl Populate XML_CFLAGS and XML_LIBS with infomation for
dnl linking with libxml2
AC_DEFUN([XIPH_PATH_XML],
[dnl
AC_MSG_RESULT([checking for XML configuration])
AC_ARG_VAR([XMLCONFIG],[XML configuration program])
AC_ARG_WITH(xml-config,
    [AC_HELP_STRING([--with-xml-config=PATH],
                    [use xml-config in PATH to find libxml])],
    [XMLCONFIG="$withval"],
    [AC_PATH_PROGS(XMLCONFIG, [xml2-config xml-config], "")]
)
if test "x$XMLCONFIG" = "x"; then
    AC_MSG_ERROR([XML configuration could not be found])
fi
if ! test -x "$XMLCONFIG"; then
    AC_MSG_ERROR([$XMLCONFIG cannot be executed])
fi
XML_LIBS="$($XMLCONFIG --libs)"
XML_CFLAGS="$($XMLCONFIG --cflags)"
ac_xml_save_LIBS="$LIBS"
ac_xml_save_CFLAGS="$CFLAGS"
LIBS="$XML_LIBS $LIBS"
CFLAGS="$CFLAGS $XML_CFLAGS"
AC_CHECK_FUNC(xmlParseFile,, [AC_MSG_ERROR([Unable to link with libxml])])
CFLAGS="$ac_xml_save_CFLAGS"
LIBS="$ac_xml_save_LIBS"
])

dnl XIPH_PATH_XSLT
dnl Populate XSLT_CFLAGS and XSLT_LIBS with infomation for
dnl linking with libxml2
AC_DEFUN([XIPH_PATH_XSLT],
[dnl
AC_ARG_VAR([XSLTCONFIG],[XSLT configuration program])
AC_ARG_WITH(xslt-config,
    [AC_HELP_STRING([--with-xslt-config=PATH],
                    [use xslt-config in PATH to find libxslt])],
    [XSLTCONFIG="$withval"],
    [AC_PATH_PROGS(XSLTCONFIG, [xslt-config], "")]
)
if test "x$XSLTCONFIG" = "x"; then
    AC_MSG_ERROR([XSLT configuration could not be found])
fi
if ! test -x "$XSLTCONFIG"; then
    AC_MSG_ERROR([$XSLTCONFIG cannot be executed])
fi
XSLT_LIBS="$($XSLTCONFIG --libs)"
XSLT_CFLAGS="$($XSLTCONFIG --cflags)"
ac_xslt_save_LIBS="$LIBS"
ac_xslt_save_CFLAGS="$CFLAGS"
LIBS="$XSLT_LIBS $LIBS"
CFLAGS="$CFLAGS $XSLT_CFLAGS"
AC_CHECK_FUNCS([xsltSaveResultToString])
CFLAGS="$ac_xslt_save_CFLAGS"
LIBS="$ac_xslt_save_LIBS"
])
