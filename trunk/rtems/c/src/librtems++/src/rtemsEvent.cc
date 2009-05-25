/*
  ------------------------------------------------------------------------
  $Id: rtemsEvent.cc,v 1.2 2001-09-27 12:02:05 chris Exp $
  ------------------------------------------------------------------------

  COPYRIGHT (c) 1997
  Objective Design Systems Ltd Pty (ODS)
  All rights reserved (R) Objective Design Systems Ltd Pty
  
  The license and distribution terms for this file may be found in the
  file LICENSE in this distribution or at
  http://www.OARcorp.com/rtems/license.html.

  ------------------------------------------------------------------------

  See header file.

  ------------------------------------------------------------------------
*/

#include <rtems++/rtemsEvent.h>

/* ----
    rtemsEvent
*/

rtemsEvent::rtemsEvent(const char *name_str, rtems_unsigned32 node)
  : name(rtems_build_name('S', 'E', 'L', 'F')),
    id(RTEMS_SELF)
{
  connect(name_str, node);
}

rtemsEvent::rtemsEvent(const rtemsEvent& event)
{
  name = event.name;
  id = event.id;
}

rtemsEvent::rtemsEvent()
  : name(rtems_build_name('S', 'E', 'L', 'F')),
    id(RTEMS_SELF)
{
}

rtemsEvent::~rtemsEvent()
{
}

const rtemsEvent& rtemsEvent::operator=(const rtemsEvent& event)
{
  name = event.name;
  id = event.id;

  return *this;
}

const rtems_status_code rtemsEvent::connect(const char *name_str,
                                            const rtems_unsigned32 node)
{
  name = rtems_build_name(name_str[0],
                          name_str[1],
                          name_str[2],
                          name_str[3]);
  
  set_status_code(rtems_task_ident(name, node, &id));

  if (unsuccessful())
  {
    name = rtems_build_name('S', 'E', 'L', 'F');
    id = RTEMS_SELF;
  }
  
  return last_status_code();
}
