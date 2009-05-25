/*
 *  This test file is used to verify that the header files associated with
 *  invoking this function are correct.
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: pthread06.c,v 1.2 2001-09-27 12:02:25 chris Exp $
 */

#include <pthread.h>
 
#ifndef _POSIX_THREADS
#error "rtems is supposed to have pthread_getstacksize"
#endif

#ifndef _POSIX_THREAD_ATTR_STACKSIZE
#error "rtems is supposed to have pthread_getstacksize"
#endif

void test( void )
{
  pthread_attr_t  attribute;
  size_t          size;
  int             result;

  result = pthread_attr_getstacksize( &attribute, &size );
}
