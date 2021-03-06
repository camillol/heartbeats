/** \file 
 *  \brief Example: Something more interesting
 *  \author Hank Hoffmann 
 *  \version 1.0
 *  \example system.c
 *  A More Interesting Example
 */

#include <stddef.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
/**/
#include <unistd.h>
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <getopt.h>
#include <cpufreq.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heart_rate_monitor.h"
#include <assert.h>
#include <wait.h>

#define _(String) gettext (String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

#define LINE_LEN 10


heart_rate_monitor_t heart;

typedef struct {
  int tag;
  double rate;
} heart_data_t;



//static int pipe_set_up = 0;

/**
       * 
       * @param apps
       * @return count integer: number of heartbeats
       */
int get_heartbeat_apps(int* apps) {
  pid_t pid;
  int rv;
  int	commpipe[2];		/* This holds the fd for the input & output of the pipe */
  char string[1024][100];
  int count = 0;
  
  /* Setup communication pipeline first */
  if(pipe(commpipe)){
    fprintf(stderr,"Pipe error!\n");
    exit(1);
  }
  
  /* Attempt to fork and check for errors */
  if( (pid=fork()) == -1){
    fprintf(stderr,"Fork error. Exiting.\n");  /* something went wrong */
    exit(1);        
  }
  
  if(pid){
    /* A positive (non-negative) PID indicates the parent process */
    dup2(commpipe[0],0);	
    close(commpipe[1]);		
    while(fgets(string[count], 100, stdin)) {
      apps[count] = atoi(string[count]);
     count++;
    }
    
    //printf("From the system: found %d apps\n", count);
    //printf("From the system: app is %d\n", apps[0]);
    wait(&rv);				/* Wait for child process to end */
    //fprintf(stderr,"Child exited with a %d value\n",rv);
  }
  else{
    /* A zero PID indicates that this is the child process */
    dup2(commpipe[1],1);	/* Replace stdout with the out side of the pipe */
    close(commpipe[0]);		/* Close unused side of pipe (in side) */
    /* Replace the child fork with a new process */
    //FIXME
    //if(execl("/bin/ls","/bin/ls","/scratch/etc/heartbeat/heartbeat-enabled-apps/",(char*) NULL) == -1){
    if(execl("/bin/ls","/bin/ls",getenv("HEARTBEAT_ENABLED_DIR"),(char*) NULL) == -1){
      fprintf(stderr,"execl Error!");
      exit(1);
    }
  }

  close(commpipe[0]);
  return count;

}

static int get_cpus(){
FILE *fp;
	char value[LINE_LEN];
	unsigned int ret = 0;
	unsigned int cpunr = 0;

	fp = fopen("/proc/stat", "r");
	if(!fp) {
		printf(gettext("Couldn't count the number of CPUs (%s: %s), assuming 1\n"), "/proc/stat", strerror(errno));
		return 1;
	}

	while (!feof(fp)) {
		if (!fgets(value, LINE_LEN, fp))
			continue;
		value[LINE_LEN - 1] = '\0';
		if (strlen(value) < (LINE_LEN - 2))
			continue;
		if (strstr(value, "cpu "))
			continue;
		if (sscanf(value, "cpu%d ", &cpunr) != 1)
			continue;
		if (cpunr > ret)
			ret = cpunr;
	}
	fclose(fp);

	/* cpu count starts from 0, on error return 1 (UP) */
	return (ret+1);
}

int get_current_cores_assigned(pid_t pid)
{
	char buf[256];
	FILE *proc;
	unsigned int affinity;
	int result;
	
	snprintf(buf, sizeof(buf), "taskset -p %d | sed 's/.* //'", (int)pid);
	proc = popen(buf, "r");
	pclose(proc);
	
	result = 0;
	while (affinity) {
		result += affinity & 0x01;
		affinity /= 2;
	}
	return result;
}


void print_status(heartbeat_record_t *current, int wait_for, int cores, char action)
{
	printf("%lld\t%.3f\t%u\t%c\t%d\n", current->beat, current->window_rate, cores, action, wait_for);
}
/**
       * 
       */
  int main(int argc, char** argv) {
  int n = 0;
  int i;
  const int MAX = atoi(argv[1]);
  int ncpus=0;
  int nprocs = 1;


  int apps[1024];

setlinebuf(stdout);
ncpus=get_cpus();




   if(getenv("HEARTBEAT_ENABLED_DIR") == NULL) {
     fprintf(stderr, "ERROR: need to define environment variable HEARTBEAT_ENABLED_DIR (see README)\n");
     return 1;
   }

  heart_data_t* records = (heart_data_t*) malloc(MAX*sizeof(heart_data_t));
  int last_tag = -1;

  while(n == 0) {
    n = get_heartbeat_apps(apps);
  }

  //printf("apps[0] = %d\n", apps[0]);

  // For this test we only allow one heartbeat enabled app
 // assert(n==1);
  if (n>1) {
     printf("too many apps!!!!!!!!!!!!!!\n");
     exit(2);
  }
 /* sleep(5);*/

#if 1
  int rc = heart_rate_monitor_init(&heart, apps[0]);

  if (rc != 0)
    printf("Error attaching memory\n");

  printf("buffer depth is %lld\n", (long long int) heart.state->buffer_depth);

  i = 0;

printf("beat\trate\tcores\tact\twait\n");

  int64_t window_size =  hrm_get_window_size(&heart);
  int wait_for = (int) window_size;
  int current_beat = 0;
  
  int current_beat_prev= 0;

  printf("Current beat is %d, wait_for = %d\n", current_beat, wait_for);

  // return 1; 
 
  nprocs=get_current_cores_assigned(apps[0]);
    
  while(current_beat < MAX) {
    int rc = -1;
    heartbeat_record_t record;
    char command[256];


      while (rc != 0 || record.window_rate == 0.0000 ){
	rc = hrm_get_current(&heart, &record);
	current_beat = record.beat;
      /*  printf("(skipping)Current beat is %d, wait_for = %d, %f\n", current_beat, wait_for, record.window_rate);*/
      }
   

       if(current_beat_prev == current_beat)
      continue;
         current_beat_prev= current_beat;

      if( current_beat < wait_for){
         print_status(&record, wait_for, nprocs, '.');
	continue;}

   /*   printf("Current beat is %d, wait_for = %d, %f\n", current_beat, wait_for, record.window_rate);*/

      if(record.window_rate < hrm_get_min_rate(&heart)) {
        wait_for = current_beat + window_size;	
        if(nprocs<ncpus){
	nprocs++;
	sprintf(command, "taskset -pc 0-%d %d > /dev/null",nprocs-1,apps[0]);
	/*printf("Executing %s\n", command);*/
	system(command);
	
        print_status(&record, wait_for, nprocs, '+');
        }
        else{
           print_status(&record, wait_for, nprocs, 'M');
           }
      }
      else if(record.window_rate > hrm_get_max_rate(&heart)) {
        wait_for = current_beat + window_size;	
         if(nprocs>0) {
	    nprocs--;
	    sprintf(command, "taskset -pc 0-%d %d > /dev/null",nprocs-1,apps[0]);
	    system(command);	
	    wait_for = current_beat + window_size;
            print_status(&record, wait_for, nprocs, '-');
           }
         else{
           print_status(&record, wait_for, nprocs, 'm');
           }
      }
      else {
	wait_for = current_beat+1;
        print_status(&record, wait_for, nprocs, '=');
      }
     
      records[i].tag = current_beat;
      records[i].rate = record.window_rate;
      i++;
   

  }

  //printf("System: Global heart rate: %f, Current heart rate: %f\n", heart.global_heartrate, heart.window_heartrate);

 /* for(i = 0; i < MAX; i++) {
    printf("%d, %f\n", records[i].tag, records[i].rate);
  }*/
  heart_rate_monitor_finish(&heart);
#endif

  return 0;
}
