#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/mutex.h>
#include <asm-generic/uaccess.h>
#include <syscalls.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cameron Jackson, Christian Baez, Michael Garcia-Rivas");
MODULE_DESCRIPTION("elevator module");


#define ENTRY_NAME "elevator"
#define PERMS 0644
#define PARENT NULL
#define MAX_WEIGHT 16
#define MAX_PASS 8
#define FLOOR_WAIT 2
#define PASSENGER_WAIT 1
#define MIN_FLOOR 1
#define MAX_FLOOR 10

struct passengers
{
  struct list_head list; /* used for linked list */
  int punit_;
  int wunit_;
  int pnum_; /* for list this is passenger number. for total this is total number */
  int ptype_; /* unused for totals */
  int sfloor_;
  int dfloor_;
  int riding_;
};

struct elevator
{
  char *state_;
  int currfloor_;
  int nextfloor_;
  int isempty_;
  int passserviced_;
   struct passengers totalriding_; /* struct containing totals for elevator */
  struct passengers totalwaiting_; /* struct containing totals for queue */
};

/* struct elevator my_elevator; */
static struct elevator my_elevator;
static struct file_operations fops;
static int read_p;
static char *elevatormessage;
static struct task_struct *elevatorthread;
static int ethread_running = 0;
/* static struct task_struct *servicerthread; */

static LIST_HEAD(waitinglist);
static LIST_HEAD(ridinglist);
static struct passengers *waiting;
/* static struct passengers *riding; */
static struct passengers *w_tmp, *r_tmp;
static struct mutex lock;

static int passengerarrived;

static int amnt_waiting[] = {0,0,0,0,0,0,0,0,0,0};
static int load_waiting[] = {0,0,0,0,0,0,0,0,0,0};
static int amnt_serviced[] = {0,0,0,0,0,0,0,0,0,0};

static int elevator_run(void *name);

int start(char *);
EXPORT_SYMBOL(start);
int start(char *newstate)
{
  if( newstate != NULL)
  {
    strcpy(my_elevator.state_, newstate);
  }
  my_elevator.totalriding_.punit_ = 0;
  my_elevator.totalriding_.wunit_ = 0;
  my_elevator.currfloor_ = 1;
  elevatorthread = kthread_run(elevator_run, NULL, "elevatorthread");
  ethread_running = 1;
  if (IS_ERR(elevatorthread))
  {
    printk("ERROR! kthread_run\n");
    return PTR_ERR(elevatorthread);
  }

  printk("State function\n");
  printk("state: %s\n", my_elevator.state_);

  return 0;
}

int stop(char *);
EXPORT_SYMBOL(stop);
int stop(char *newstate)
{
  int ret;
  if( newstate != NULL)
  {
    strcpy(my_elevator.state_, newstate);
  }
  ethread_running = 0;
  ret = kthread_stop(elevatorthread);
  if (ret != -EINTR)
    printk("elevatorthread has stopped\n");  
  printk("State function\n");
  printk("state: %s\n", my_elevator.state_);

  return 0;
}


int get_state(void);
EXPORT_SYMBOL(get_state);
int get_state(void)
{
  return ethread_running;
}

long process_request(int, int, int);
EXPORT_SYMBOL(process_request);
long process_request(int passenger_type, int start_floor, int destination_floor)
{
  
  waiting = kmalloc(sizeof(struct passengers)*20, __GFP_WAIT | __GFP_IO | __GFP_FS);
/* Adult (0): 1 passenger unit, 1 weight unit
   Child (1): 1 passenger unit, 1/2 weight unit
   Bellhop (2): 2 passenger units, 2 weight unit
   Room Service (3): 1 passenger unit, 2 weight units */
  printk("Process_request\n");
  waiting->ptype_ = passenger_type;
  waiting->sfloor_ = start_floor;
  waiting->dfloor_ = destination_floor;
  waiting->riding_ = 0;
  switch(passenger_type)
  {
  case 0:
    waiting->punit_ = 1;
    waiting->wunit_ = 2;
    break;
  case 1:
    waiting->punit_ = 1;
    waiting->wunit_ = 1;
    break;
  case 2:
    waiting->punit_ = 2;
    waiting->wunit_ = 4;
    break;
  case 3:
    waiting->punit_ = 1;
    waiting->wunit_ = 4;
    break;
  default:
    waiting->punit_ = 0;
    waiting->wunit_ = 0;
  }
  amnt_waiting[start_floor-1] = amnt_waiting[start_floor-1] + waiting->punit_;
  load_waiting[start_floor-1] = load_waiting[start_floor-1] + waiting->wunit_;
  passengerarrived = 1;

  list_add_tail(&waiting->list,&waitinglist);

  my_elevator.totalwaiting_.pnum_ = my_elevator.totalwaiting_.pnum_ + 1;
  my_elevator.totalwaiting_.punit_ = my_elevator.totalwaiting_.punit_ + waiting->punit_;
  my_elevator.totalwaiting_.wunit_ = my_elevator.totalwaiting_.wunit_ + waiting->wunit_;
  return 0;
}

static int elevator_run(void *name)
{
  struct list_head *pos, *n;
  int weight, pass, w_tmp_direc;
  int elev_direc;
  int index = 0;
  int loading = 0;
  int unloading = 0;

  // waiting list
  w_tmp = kmalloc(sizeof(struct passengers),__GFP_WAIT | __GFP_IO | __GFP_FS);
  // riding list
  r_tmp = kmalloc(sizeof(struct passengers),__GFP_WAIT | __GFP_IO | __GFP_FS);

  while(!kthread_should_stop())
  {
    if (list_empty(&waitinglist) && list_empty(&ridinglist))
    {
      if (ethread_running == 1)
        strcpy(my_elevator.state_,"IDLE");
      else
        strcpy(my_elevator.state_,"STOPPED");
    }
    else if (!list_empty(&waitinglist) || !list_empty(&ridinglist))
    {
      for(; my_elevator.currfloor_ < 10; my_elevator.currfloor_ = my_elevator.currfloor_ + 1)
      {
	
	strcpy(my_elevator.state_,"UP");
        elev_direc = 1;
	if (my_elevator.currfloor_ < 10)
	  my_elevator.nextfloor_ = my_elevator.currfloor_ + 1;
	list_for_each_safe(pos,n,&ridinglist)
	  /* unload passengers */
        {
	  r_tmp = list_entry(pos,struct passengers, list);
	  weight = r_tmp->wunit_;
	  pass = r_tmp->punit_;
          if( r_tmp->dfloor_ == my_elevator.currfloor_ )
          {
            unloading = 1;
            strcpy(my_elevator.state_,"UNLOADING");
	    my_elevator.totalriding_.wunit_ = my_elevator.totalriding_.wunit_ - weight;
	    my_elevator.totalriding_.punit_ = my_elevator.totalriding_.punit_ - pass;
	    amnt_serviced[r_tmp->sfloor_-1] = amnt_serviced[r_tmp->sfloor_-1] + 1;
	    list_del(pos);
          }
        } // list_for_each_safe unload
        /* sleep for 1 second if unloading passengers */
        if(unloading == 1) {
          ssleep(1);
          unloading = 0;
        }

        strcpy(my_elevator.state_,"UP");

	list_for_each_safe(pos,n,&waitinglist)
	  /* load passengers */
	{
	  w_tmp = list_entry(pos,struct passengers, list);
	  weight = w_tmp->wunit_ + my_elevator.totalriding_.wunit_;
	  pass = w_tmp->punit_ + my_elevator.totalriding_.punit_;
	  w_tmp_direc = (w_tmp->dfloor_ - w_tmp->sfloor_) > 0;
	  if (
                 w_tmp->sfloor_ == my_elevator.currfloor_ &&
                weight <= MAX_WEIGHT && pass <= MAX_PASS &&
                     (w_tmp_direc == elev_direc) &&
                      ethread_running == 1
                                                              )
	  {
            loading = 1;
            strcpy(my_elevator.state_,"LOADING");
	    my_elevator.totalriding_.punit_ = pass;
	    my_elevator.totalriding_.wunit_ = weight;
	    amnt_waiting[w_tmp->sfloor_-1] = amnt_waiting[w_tmp->sfloor_-1] - w_tmp->punit_;
	    load_waiting[w_tmp->sfloor_-1] = load_waiting[w_tmp->sfloor_-1] - w_tmp->wunit_;
	    index = index + 1;

	    list_move_tail(pos,&ridinglist);

	  }
	} // list_for_each_safe() LOADING

        // sleep for 1 sec if loading
        if(loading == 1) {
          ssleep(1);
          loading = 0;
        }

        strcpy(my_elevator.state_,"UP");
        ssleep(2);
      }// for UP

      for(; my_elevator.currfloor_ > 1;my_elevator.currfloor_ = my_elevator.currfloor_ - 1)
      {
	if (my_elevator.currfloor_ > 1)
	  my_elevator.nextfloor_ = my_elevator.nextfloor_ - 1;
	strcpy(my_elevator.state_,"DOWN");
        elev_direc = 0;
	ssleep(2);

	list_for_each_safe(pos,n,&ridinglist)
        /* unload passengers */
        {
	  r_tmp = list_entry(pos,struct passengers, list);
	  weight = r_tmp->wunit_;
	  pass = r_tmp->punit_;
          if( r_tmp->dfloor_ == my_elevator.currfloor_ )
          {
            unloading = 1;
 	    strcpy(my_elevator.state_,"UNLOADING");
	    my_elevator.totalriding_.wunit_ = my_elevator.totalriding_.wunit_ - weight;
	    my_elevator.totalriding_.punit_ = my_elevator.totalriding_.punit_ - pass;
	    amnt_serviced[r_tmp->sfloor_-1] = amnt_serviced[r_tmp->sfloor_-1] + 1;
	    list_del(pos);
          }
        } // list_for_each_safe UNLOADING

        // sleep for 1 sec if unloading
        if(unloading == 1) {
          ssleep(1);
          unloading = 0;
        }

	strcpy(my_elevator.state_,"DOWN");
	list_for_each_safe(pos,n,&waitinglist)
	{
	  w_tmp = list_entry(pos,struct passengers, list);
	  weight = w_tmp->wunit_ + my_elevator.totalriding_.wunit_;
	  pass = w_tmp->punit_ + my_elevator.totalriding_.punit_;
	  w_tmp_direc = (w_tmp->dfloor_ - w_tmp->sfloor_) > 0;
	  if (
                   w_tmp->sfloor_ == my_elevator.currfloor_ && 
                   weight <= MAX_WEIGHT && pass <= MAX_PASS && 
                           (w_tmp_direc == elev_direc) &&
                               ethread_running == 1
                                                                   )
	  {
            loading = 1;
   	    strcpy(my_elevator.state_,"LOADING");
	    my_elevator.totalriding_.punit_ = pass;
	    my_elevator.totalriding_.wunit_ = weight;
	    amnt_waiting[w_tmp->sfloor_-1] = amnt_waiting[w_tmp->sfloor_-1] - w_tmp->punit_;
	    load_waiting[w_tmp->sfloor_-1] = load_waiting[w_tmp->sfloor_-1] - w_tmp->wunit_;
	    index = index + 1;

	    list_move_tail(pos,&ridinglist);

	  } // if
	} // list_for_each_safe()
        // sleep for 1 sec if loading
        if(loading == 1) {
          ssleep(1);
          loading = 0;
        }

        ssleep(2);
        strcpy(my_elevator.state_,"DOWN");
      } // for()
    } // if

  }
  if (ethread_running == 1)
    strcpy(my_elevator.state_,"IDLE");
  else
    strcpy(my_elevator.state_,"STOPPED");
  return 0;
}

static int elevator_open(struct inode *sp_inode, struct file *sp_file) {
  printk("elevator called open\n");
  read_p = 1;
  if (my_elevator.state_ == NULL) {
    printk("ERROR, elevator_open\n");
    return -ENOMEM;
  }
  return 0;
}

static ssize_t elevator_read(struct file *sp_file, char __user *buf, size_t size, loff_t *offset) {
  int len, i;
  char *load_waiting_str[10];
  char *current_load_str;

  read_p = !read_p;
  if (read_p) {
    return 0;
  }

  current_load_str = kmalloc(sizeof(char*)*10,__GFP_WAIT | __GFP_IO | __GFP_FS);
  if ((my_elevator.totalriding_.wunit_%2) == 0) /* even */
    snprintf(current_load_str,10,"%d",my_elevator.totalriding_.wunit_/2);      
  else
    snprintf(current_load_str,10,"%d.5",my_elevator.totalriding_.wunit_/2);      

  for (i = 0; i < 10; ++i)
  {
    load_waiting_str[i] = kmalloc(sizeof(char*)*10,__GFP_WAIT | __GFP_IO | __GFP_FS);
    if ((load_waiting[i]%2) == 0) /* even */
      snprintf(load_waiting_str[i],10,"%d",load_waiting[i]/2);      
    else
      snprintf(load_waiting_str[i],10,"%d.5",load_waiting[i]/2);
  }
  snprintf(elevatormessage, 2000,
	   "Movement state: %s\nCurrent floor: %d\nNext floor: %d\nCurrent load: pUnits = %d, wUnits = %s\nWaiting Floor 01: punits = %d, wunits = %s\nWaiting Floor 02: punits = %d, wunits = %s\nWaiting Floor 03: punits = %d, wunits = %s\nWaiting Floor 04: punits = %d, wunits = %s\nWaiting Floor 05: punits = %d, wunits = %s\nWaiting Floor 06: punits = %d, wunits = %s\nWaiting Floor 07: punits = %d, wunits = %s\nWaiting Floor 08: punits = %d, wunits = %s\nWaiting Floor 09: punits = %d, wunits = %s\nWaiting Floor 10: punits = %d, wunits = %s\nServiced Floor 01: %d\nServiced Floor 02: %d\nServiced Floor 03: %d\nServiced Floor 04: %d\nServiced Floor 05: %d\nServiced Floor 06: %d\nServiced Floor 07: %d\nServiced Floor 08: %d\nServiced Floor 09: %d\nServiced Floor 10: %d\n",
	   my_elevator.state_, my_elevator.currfloor_, my_elevator.nextfloor_,
	   my_elevator.totalriding_.punit_,current_load_str,
	   amnt_waiting[0],load_waiting_str[0], amnt_waiting[1], load_waiting_str[1], 
	   amnt_waiting[2], load_waiting_str[2], amnt_waiting[3], load_waiting_str[3], 
	   amnt_waiting[4], load_waiting_str[4], amnt_waiting[5], load_waiting_str[5], 
	   amnt_waiting[6], load_waiting_str[6], amnt_waiting[7], load_waiting_str[7], 
	   amnt_waiting[8], load_waiting_str[8], amnt_waiting[9], load_waiting_str[9],
	   amnt_serviced[0], amnt_serviced[1], amnt_serviced[2], amnt_serviced[3],
	   amnt_serviced[4], amnt_serviced[5], amnt_serviced[6], amnt_serviced[7],
	   amnt_serviced[8], amnt_serviced[9]);

  len = strlen(elevatormessage);
  copy_to_user(buf,elevatormessage,len);
  return len;
}

static int elevator_release(struct inode *sp_inode, struct file *sp_file) {
  printk("elevator called release\n");
  return 0;
}

static int elevator_init(void) {
  waiting = NULL;
  w_tmp = NULL;
  r_tmp = NULL;

  printk("Inserting Elevator\n");
  elevator_syscalls_create();
  fops.open = elevator_open;
  fops.read = elevator_read;
  fops.release = elevator_release;

  passengerarrived = 0;
  mutex_init(&lock);
  INIT_LIST_HEAD(&waitinglist);
  INIT_LIST_HEAD(&ridinglist);
  my_elevator.currfloor_ = 0;
  my_elevator.nextfloor_ = 0;
  my_elevator.totalwaiting_.punit_ = 0;
  my_elevator.totalwaiting_.wunit_ = 0;
  my_elevator.totalwaiting_.pnum_ = 0;
  my_elevator.totalwaiting_.ptype_ = 0;
  my_elevator.totalriding_.punit_ = 0;
  my_elevator.totalriding_.wunit_ = 0;
  my_elevator.totalriding_.pnum_ = 0;
  my_elevator.totalriding_.ptype_ = 0;
  my_elevator.passserviced_ = 0;
  my_elevator.isempty_ = 1; /* empty */

  elevatormessage = kmalloc(sizeof(char)*2000, __GFP_WAIT | __GFP_IO | __GFP_FS);
  my_elevator.state_ = kmalloc(sizeof(char)*20, __GFP_WAIT | __GFP_IO | __GFP_FS);

  if (!proc_create(ENTRY_NAME,PERMS,NULL,&fops))
  {
    printk("ERROR! proc_create\n");
    remove_proc_entry(ENTRY_NAME,NULL);
    return -ENOMEM;
  }

  strcpy(my_elevator.state_,"STOPPED");
  return 0;
}

static void elevator_exit(void) {
  int ret;
  printk("Removing elevator\n");

  if(ethread_running == 1){
    ret = kthread_stop(elevatorthread);
    if (ret != -EINTR)
      printk("elevatorthread has stopped\n");
  }

  remove_proc_entry(ENTRY_NAME,NULL);

  kfree(elevatormessage);
  kfree(my_elevator.state_);

  if (w_tmp != NULL)
    kfree(w_tmp);
  if (r_tmp != NULL)
    kfree(r_tmp);

  elevator_syscalls_remove();
}

module_init(elevator_init);
module_exit(elevator_exit);

