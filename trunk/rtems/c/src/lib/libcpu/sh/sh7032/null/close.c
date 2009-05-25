/*  null_close
 *
 *  This routine is the null device driver close routine.
 *
 *  Derived from rtems' stub driver.
 *
 *  Author: Ralf Corsepius (corsepiu@faw.uni-ulm.de)
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargb - pointer to close parameter block
 *
 *  Output parameters:
 *    rval       - NULL_SUCCESSFUL
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: close.c,v 1.2 2001-09-27 12:01:33 chris Exp $
 */

#include <rtems.h>
#include <sh/null.h>

rtems_device_driver null_close(
  rtems_device_major_number major,
  rtems_device_minor_number minor,
  void *pargp
)
{
  return NULL_SUCCESSFUL;
}
