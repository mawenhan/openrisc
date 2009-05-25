/*  Delayed_send_event
 *
 *  This routine is a timer service routine which sends an event to a task.
 *
 *  Input parameters:  NONE
 *
 *  Output parameters:  NONE
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: delay.c,v 1.2 2001-09-27 12:02:23 chris Exp $
 */

#include "system.h"

rtems_timer_service_routine Delayed_send_event(
  rtems_id  timer_id,
  void     *ignored
)
{
  rtems_status_code status;

  status = rtems_event_send(
    Task_id[ rtems_get_index( timer_id ) ],
    RTEMS_EVENT_16
  );
  directive_failed( status, "rtems_event_send" );
}
