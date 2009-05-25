/*
 *  Watchdog Handler
 *
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: watchdog.c,v 1.2 2001-09-27 11:59:34 chris Exp $
 */

#include <rtems/system.h>
#include <rtems/score/isr.h>
#include <rtems/score/watchdog.h>

/*PAGE
 *
 *  _Watchdog_Handler_initialization
 *
 *  This routine initializes the watchdog handler.
 *
 *  Input parameters:  NONE
 *
 *  Output parameters: NONE
 */

void _Watchdog_Handler_initialization( void )
{
  _Watchdog_Sync_count = 0;
  _Watchdog_Sync_level = 0;
  _Watchdog_Ticks_since_boot = 0;

  _Chain_Initialize_empty( &_Watchdog_Ticks_chain );
  _Chain_Initialize_empty( &_Watchdog_Seconds_chain );
}
