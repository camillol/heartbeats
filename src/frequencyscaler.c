/** \file 
 *  \author Miriam B. Russom
 */

/**/
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

/**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heart_rate_monitor.h"
#include <assert.h>
#include <wait.h>
/**/


unsigned long min, max;
int current =0;
/**/

heart_rate_monitor_t heart;

typedef struct {
  int tag;
  double rate;
} heart_data_t;

typedef struct{
unsigned int freq;
char string[3];
} freq_data;




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



/**/

static int get_hardware_limits(unsigned int cpu) {
	
	if (cpufreq_get_hardware_limits(cpu, &min, &max))
	  return -EINVAL;

        printf("Your frequency limits is:\n");
        printf("MIN: %luMHZ and MAX:%luMHZ\n", min,max);
	return 0;
}

static int get_policy(unsigned int cpu) {
	struct cpufreq_policy *policy = cpufreq_get_policy(cpu);
	if (!policy)
		return -EINVAL;
	//printf("%lu %lu %s\n", policy->min, policy->max, policy->governor);

        if (policy->governor != "userspace"){
                cpufreq_modify_policy_governor(cpu, "userspace");
                struct cpufreq_policy *policy = cpufreq_get_policy(cpu);
                 printf("%lu %lu %s\n", policy->min, policy->max, policy->governor);
                } 
      
	if (!policy)
		return -EINVAL;

	    cpufreq_put_policy(policy);
	return 0;
}


/*static void writestr(const char *name, const char *val)
{
	int fd = sysfile(name, 0, O_WRONLY);

	write(fd, val, strlen(val));

	close(fd);
}
*/

static unsigned int get_speed(unsigned long speed)
{
	unsigned long tmp;

if (speed > 1000000) {
		tmp = speed % 10000;
		if (tmp >= 5000)
			speed += 10000;
		printf ("%u.%02u GHz", ((unsigned int) speed/1000000),
			((unsigned int) (speed%1000000)/10000));
	} else
           if (speed > 100000) {
		tmp = speed % 1000;
		if (tmp >= 500)
			speed += 1000;
		printf ("%u MHz", ((unsigned int) speed / 1000));
	} else if (speed > 1000) {
		tmp = speed % 100;
		if (tmp >= 50)
			speed += 100;
		printf ("%u.%01u MHz", ((unsigned int) speed/1000),
			((unsigned int) (speed%1000)/100));
	} else
		printf ("%lu kHz", speed);

	return  (unsigned int)speed/1000;
}


static int get_available_freqs_size(struct cpufreq_available_frequencies *freq){
  int i=0;
     while (freq) {
			i++;
			freq = freq->next;
		}
		
     return i;

}


static int store_available_freqs(struct cpufreq_available_frequencies *frequencies, unsigned long* freq_array, int count){
  int i=0;

   while(frequencies && i <= count){
            freq_array[i] = frequencies->frequency;
           get_speed(freq_array[i]);
            i++;
            frequencies= frequencies->next;
          }
   return 0;
}



static unsigned long get_init_frequency(unsigned long* available_freqs_array, int cpu1){
int i=0;
unsigned long curent_init_freq;

      curent_init_freq = cpufreq_get_freq_kernel(cpu1);

      while(available_freqs_array[i]!=curent_init_freq ){
                                                       i++;
                                                   }
        return i;

}
/**/

/**
       * 
       */
int main(int argc, char** argv) {
  int n = 0;
  int i;
  const int MAX = atoi(argv[1]);

  int apps[1024];

/**/
extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0, cont = 1;
	unsigned int cpu = 0;
	//unsigned int cpu_defined = 0;
        unsigned long  min_available_freq = 0;
        unsigned long max_available_freq = 0;  
        unsigned long current_freq = 0;
        unsigned long initial_freq = 0;
        struct cpufreq_available_frequencies *freqs;
        int retr =0;


  ret= get_hardware_limits(cpu);

  min_available_freq = min;
  max_available_freq = max;

  ret= get_policy(cpu);

  freqs = cpufreq_get_available_frequencies(cpu);

/*if (freqs==null){
  goto out;
}*/

  current = get_available_freqs_size(freqs);
/*("available frequency steps: ");
		while (freqs->next) {
			 print_speed(freqs->frequency);
			printf(", ");
			freqs = freqs->next;
		}
		print_speed(freqs->frequency);
		printf("\n");
		//cpufreq_put_available_frequencies(freqs);*/


  unsigned long* available_freqs = (unsigned long *) malloc(current*sizeof(unsigned long));


  ret = store_available_freqs( freqs, available_freqs, current);

  int current_counter = get_init_frequency(available_freqs, cpu);

  /*if (ret!=0){
               goto out;
                }*/


  current_freq = cpufreq_get_freq_kernel(cpu);
  printf("Current frequency is: %luMHZ\n", get_speed(current_freq)); 
  current_freq = cpufreq_get_freq_kernel(1);
  printf("Current frequency is: %luMHZ\n", get_speed(current_freq)); 
current_freq = cpufreq_get_freq_kernel(2);
  printf("Current frequency is: %luMHZ\n", get_speed(current_freq)); 
  printf("Locate current frequency: %lu\n", get_speed(available_freqs[current_counter]));
  printf("%d",ret);


   if(getenv("HEARTBEAT_ENABLED_DIR") == NULL) {
     fprintf(stderr, "ERROR: need to define environment variable HEARTBEAT_ENABLED_DIR (see README)\n");
     return 1;
   }

  heart_data_t* records = (heart_data_t*) malloc(MAX*sizeof(heart_data_t));
  int last_tag = -1;

  while(n == 0) {
    n = get_heartbeat_apps(apps);
  }

  printf("apps[0] = %d\n", apps[0]);

  // For this test we only allow one heartbeat enabled app
 // assert(n==1);

  sleep(5);

#if 1
  int rc = heart_rate_monitor_init(&heart, apps[0]);

  if (rc != 0)
    printf("Error attaching memory\n");

  printf("buffer depth is %lld\n", (long long int) heart.state->buffer_depth);

  i = 0;
   printf("rate interval is %f - %f\n", hrm_get_min_rate(&heart), hrm_get_max_rate(&heart));


  int64_t window_size =  hrm_get_window_size(&heart);
  int wait_for = (int) window_size;
  int current_beat = 0;
  int current_beat_prev= 0;
  int nprocs = 1;
  unsigned int set_freq = min;
  printf("Current beat is %d, wait_for = %d\n", current_beat, wait_for);

  // return 1;  
    
  while(current_beat < MAX-1) {
    int rc = -1;
    heartbeat_record_t record;
    char command[256];


      while (rc != 0 || record.window_rate == 0.0000 ){
	rc = hrm_get_current(&heart, &record);
	current_beat = record.beat;
      }
        printf(" rc: %d, current_beat:%d \n", rc,current_beat);
       
    /*  if(current_beat_prev == current_beat)
      continue;*/

    /*Situation where doesn't happen nothing*/   
      if( current_beat < wait_for){
          current_beat_prev= current_beat;
          printf("I am in situation nothing :)");
          printf(" current_rate:%f \n", record.window_rate);
          current_freq = cpufreq_get_freq_kernel(cpu);
          printf("Current frequency is: %u\n", get_speed(current_freq));
        continue;
      }


      printf("Current beat is %d, wait_for = %d, %f\n", current_beat, wait_for, record.window_rate);

       /*Situation where frequency is up-scaled*/
      if(record.window_rate < hrm_get_min_rate(&heart)) {  
          printf("I am in situation up :)"); 
           
          current_freq = cpufreq_get_freq_kernel(cpu);
          printf("Current frequency is: %lu", current_freq);
        

         if (current_counter >0){
             current_counter--; 

             set_freq = get_speed(available_freqs[current_counter]);
           /*  cpufreq_set_frequency(cpu, set_freq);*/
	     sprintf(command, "cpufreq-set -f %luMHZ", set_freq);
	     printf("Executing %s\n", command);
             system(command);
             current_freq = cpufreq_get_freq_kernel(cpu);
             printf("Current frequency is: %u\n", get_speed(current_freq));
	     wait_for = current_beat + window_size;	}
       else printf("Maximum frequency has been reached: %luMHZ\n", get_speed(current_freq));
      }

     /*Situation where frequency is downscaled*/

      else if(record.window_rate > hrm_get_max_rate(&heart)) {
        printf("I am in situation down:)");
	
       if (current_counter < current){
           current_counter++;
          set_freq = get_speed(available_freqs[current_counter]);
        /*  cpufreq_set_frequency(cpu, set_freq);*/
          sprintf(command, " sudo cpufreq-set -f %uMHZ", set_freq);
	  printf("Executing %s\n", command);
	  system(command);
        /* freqs= freqs->next;*/
         current_freq = cpufreq_get_freq_kernel(cpu);
         printf("Current frequency is: %u\n", get_speed(current_freq));
	 wait_for = current_beat + window_size;
	}

        else {
          current_freq = cpufreq_get_freq_kernel(cpu);
          printf("Minimum frequency has been reached: %uMHZ\n", get_speed(current_freq));
        }
		
	wait_for = current_beat + window_size;
      }

      else {
	wait_for = current_beat+1;
      }
      current_beat_prev= current_beat;
      records[i].tag = current_beat;
      records[i].rate = record.window_rate;
      i++;
   

  }

  //printf("System: Global heart rate: %f, Current heart rate: %f\n", heart.global_heartrate, heart.window_heartrate);

  for(i = 0; i < MAX; i++) {
    printf("%d, %f\n", records[i].tag, records[i].rate);
  }
  heart_rate_monitor_finish(&heart);
#endif

  return 0;
}
