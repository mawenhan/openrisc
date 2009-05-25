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
 *  $Id: signal13.c,v 1.2 2001-09-27 12:02:25 chris Exp $
 */

#include <signal.h>
 
void test( void )
{
  int       how;
  sigset_t  set;
  sigset_t  oset;
  int       result;

  how = SIG_BLOCK;
  how = SIG_UNBLOCK;
  how = SIG_SETMASK;

  (void) sigemptyset( &set );

  result = sigprocmask( how, &set, &oset );
}
