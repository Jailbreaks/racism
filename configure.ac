AC_INIT(racism, 0.1, posixninja@gmail.com)
AC_PREREQ(2.61)
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 0.9.7)
PKG_CHECK_MODULES(libplist, libplist >= 0.15)
PKG_CHECK_MODULES(libzip, libzip >= 0.8)

# Checks for header files.
AC_HEADER_STDC

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_SUBST(GLOBAL_CFLAGS)

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_OUTPUT([
Makefile
src/Makefile
])

