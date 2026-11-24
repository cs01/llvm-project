#include <safec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
int main(void) {
  struct stat st; stat("/tmp", &st);
  int fd = open("/dev/null", O_RDONLY); char b[8]; read(fd, b, 8); write(1, b, 1); close(fd);
  time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(b, 8, "%Y", &tm); gmtime(&t); mktime(&tm);
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); nanosleep(&ts, NULL);
  const char *__null_terminated _Nullable c = strchr("abc", 'b'); strcmp("a", "b"); strncmp("a", "b", 1); strstr("abc", "b"); strlcpy(b, "x", 8); strspn("a","b");
  unlink("/tmp/x"); mkdir("/tmp/x", 0700); getcwd(b, 8); access("/", 0); isatty(1); 
  double d; modf(1.5, &d); pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; pthread_mutex_lock(&m); pthread_mutex_unlock(&m);
  (void)c; return atoi("3") + isalpha('a') + (int)sqrt(2.0);
}
