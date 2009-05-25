/*  Task_2
 *
 *  This routine serves as a test task.  Plays with priorities to verify
 *  that the highest priority task is always executed.
 *
 *  Input parameters:
 *    argument - task argument
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
 *  $Id: task2.c,v 1.2 2001-09-27 12:02:31 chris Exp $
 */

#include "system.h"

rtems_task Task_2(
  rtems_task_argument argument
)
{
  rtems_status_code   status;
  rtems_task_priority the_priority;
  rtems_task_priority previous_priority;

  while( FOREVER ) {

    status = rtems_task_get_note( RTEMS_SELF, RTEMS_NOTEPAD_8, &the_priority );
    directive_failed( status, "rtems_task_get_note" );
    printf(
"TA2 - rtems_task_get_note - get RTEMS_NOTEPAD_8 - current priority: %02d\n",
      the_priority
    );

    if ( --the_priority == 0 ) {
      puts( "TA2 - rtems_task_suspend - suspend TA1" );
      status = rtems_task_suspend( Task_id[ 1 ] );
      directive_failed( status, "rtems_task_suspend" );

      puts( "TA2 - rtems_task_set_priority - set priority of TA1 ( blocked )" );
      status = rtems_task_set_priority( Task_id[ 1 ], 5, &previous_priority );
      directive_failed( status, "rtems_task_set_priority" );

      status = rtems_task_delete( Task_id[ 1 ] );          /* TA1 is blocked */
      directive_failed( status, "rtems_task_delete of TA1" );

      status = rtems_task_delete( Task_id[ 3 ] );          /* TA3 is ready   */
      directive_failed( status, "rtems_task_delete of TA3" );

      status = rtems_task_delete( RTEMS_SELF );
      directive_failed( status, "rtems_task_delete of SELD" );

    } else {

      printf( "TA2 - rtems_task_set_note - set TA1's RTEMS_NOTEPAD_8: %02d\n",
              the_priority
      );
      status = rtems_task_set_note(Task_id[ 1 ], RTEMS_NOTEPAD_8, the_priority);
      directive_failed( status, "rtems_task_set_note" );

      printf( "TA2 - rtems_task_set_priority - set TA1's priority: %02d\n",
              the_priority
      );
      status = rtems_task_set_priority(
                 Task_id[ 1 ],
                 the_priority,
                 &previous_priority
      );
      directive_failed( status, "rtems_task_set_priority" );
    }
  }
}
