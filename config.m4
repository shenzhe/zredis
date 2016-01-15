dnl $Id$
dnl config.m4 for extension zredis

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(zredis, for zredis support,
dnl Make sure that the comment is aligned:
[  --with-zredis             Include zredis support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(zredis, whether to enable zredis support,
dnl Make sure that the comment is aligned:
dnl [  --enable-zredis           Enable zredis support])

if test "$PHP_ZREDIS" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-zredis -> check with-path
  SEARCH_PATH="/usr/local /usr"     # you might want to change this
  SEARCH_FOR="/include/hiredis/hiredis.h"  # you most likely want to change this
  if test -r $PHP_ZREDIS/$SEARCH_FOR; then # path given as parameter
     ZREDIS_DIR=$PHP_ZREDIS
  else # search default path list
     AC_MSG_CHECKING([for zredis files in default path])
    for i in $SEARCH_PATH ; do
      if test -r $i/$SEARCH_FOR; then
        ZREDIS_DIR=$i
        AC_MSG_RESULT(found in $i)
      fi
    done
  fi
  dnl
  if test -z "$ZREDIS_DIR"; then
      PHP_ADD_INCLUDE($PHP_ZREDIS/hiredis)
      PHP_ADD_BUILD_DIR($PHP_ZREDIS/hiredis)
      ZREDIS_CFLAGS= "-IPHP_ZREDIS/hiredis $ZREDIS_CFLAGS"
      hiredis_src="hiredis/async.c      \
                   hiredis/dict.c       \
                   hiredis/hiredis.c    \
                   hiredis/net.c        \
                   hiredis/read.c       \
                   hiredis/sds.c"        
  else 
      dnl # --with-zredis -> check for lib and symbol presence
      LIBNAME=hiredis # you may want to change this
      LIBSYMBOL=redisCommandArgv # you most likely want to change this 

      PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
      [
        PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $ZREDIS_DIR/$PHP_LIBDIR, ZREDIS_SHARED_LIBADD)
        AC_DEFINE(HAVE_ZREDISLIB,1,[ ])
      ],[
        AC_MSG_ERROR([wrong hiredis lib version or lib not found])
      ],[
        -L$ZREDIS_DIR/$PHP_LIBDIR -lm
      ])
      dnl # --with-zredis -> add include path
      PHP_ADD_INCLUDE($ZREDIS_DIR/include/hiredis)
  fi


  dnl
  PHP_SUBST(ZREDIS_CFLAGS)
  PHP_SUBST(ZREDIS_SHARED_LIBADD)

  PHP_NEW_EXTENSION(zredis, zredis.c $hiredis_src, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi
