/*  Stub_initialize
 *
 *  This routine is the terminal driver init routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargp - pointer to parameter block
 *
 *  Output parameters:
 *    rval       - STUB_SUCCESSFUL
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: init.c,v 1.2 2001-09-27 12:02:38 chris Exp $
 */

#include <rtems.h>
#include "stubdrv.h"

rtems_unsigned32 STUB_major;

rtems_device_driver Stub_initialize(
  rtems_device_major_number major,
  rtems_device_minor_number minor,
  void *pargp
)
{
  STUB_major = major;
  return STUB_SUCCESSFUL;
}
