#ifndef __ELEVATOR_SYSCALLS_H
#define __ELEVATOR_SYSCALLS_H
#include <linux/list.h>

void elevator_syscalls_create(void);
void elevator_syscalls_remove(void);

/* struct passengers */
/* { */
/*   struct list_head list; // used for linked list */
/*   int passunit_; */
/*   int weightunit_; */
/*   int passnum_; */
/* }; */

/* struct elevator */
/* { */
/*   char *state_; */
/*   int currfloor_; */
/*   int nextfloor_; */
/*   int passserviced_; */
/*   int passengertype_; */
/*   struct passengers riding_; // list of passengers riding the elevator */
/*   struct passengers waiting_; // list of passengers not riding the elevator */
/* }; */

#endif /*__ELEVATOR_SYSCALLS_H*/
