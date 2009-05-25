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
 *  $Id: sched04.c,v 1.2 2001-09-27 12:02:25 chris Exp $
 */

#include <sched.h>

#ifndef _POSIX_PRIORITY_SCHEDULING
#error "rtems is supposed to have sched_getscheduler"
#endif
 
void test( void )
{
  pid_t  pid;
  int    result;

  pid = 0;

  result = sched_getscheduler( pid );
}
