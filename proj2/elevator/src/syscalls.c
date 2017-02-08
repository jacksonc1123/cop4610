#include <linux/linkage.h>
#include <syscalls.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/kernel.h>

extern int start(char *);
extern int stop(char *);
extern int get_state(void);
extern long process_request(int, int, int);

extern long (*STUB_start_elevator)(void);
long start_elevator(void) {
  printk("Starting elevator\n");
  if (get_state() == 1)
  {
    return 1;
  }
  start("IDLE");
	
  return 0;
}

extern long (*STUB_issue_request)(int,int,int);
long issue_request(int passenger_type, int start_floor, int destination_floor) {
  printk("New request: %d, %d => %d\n", passenger_type, start_floor, destination_floor);
  if ((passenger_type > 3) || 
      (passenger_type < 0) || 
      (start_floor == destination_floor))
  {
    return 1;
  }
  process_request(passenger_type,start_floor,destination_floor);
  return 0;
}

extern long (*STUB_stop_elevator)(void);
long stop_elevator(void) {
  printk("Stopping elevator\n");
  if (get_state() == 0)
  {
    return 1;
  }

  stop("STOPPED");
  return 0;
}




void elevator_syscalls_create(void) {
  STUB_start_elevator =& (start_elevator);
  STUB_issue_request =& (issue_request);
  STUB_stop_elevator =& (stop_elevator);
}

void elevator_syscalls_remove(void) {
  STUB_start_elevator = NULL;
  STUB_issue_request = NULL;
  STUB_stop_elevator = NULL;
}
