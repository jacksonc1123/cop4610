#include <unistd.h>

int main()
{
  sleep (2);
  get_current_dir_name ();
  getpid();
  getppid();
  return 0;
}
