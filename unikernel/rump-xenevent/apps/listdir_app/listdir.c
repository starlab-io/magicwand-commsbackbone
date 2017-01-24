
#include <dirent.h> 
#include <stdio.h> 

int main(void)
{
  DIR           *d;
  struct dirent *dir;
  d = opendir("/dev");
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
      printf("%s\n", dir->d_name);
    }

    closedir(d);
  }

  return(0);
}
