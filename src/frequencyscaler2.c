/** \file 
 *  \author Miriam B. Russom
 *  \frequency scaler acting on one core at a time
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

#define _(String) gettext (String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

#define LINE_LEN 10



unsigned long min, max;
int max_freq =0;
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
      printf("app %d\n", apps[count]);

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

static int set_policy(int ncpus) {
int cpu;

  for (cpu=0; cpu <= ncpus; cpu++) {
	struct cpufreq_policy *policy = cpufreq_get_policy(cpu);
	if (!policy)
		return -EINVAL;
	//printf("%lu %lu %s\n", policy->min, policy->max, policy->governor);

        if (policy->governor != "userspace"){
                cpufreq_modify_policy_governor(cpu, "userspace");
               
                 printf("%lu %lu %s\n", policy->min, policy->max, policy->governor);
                } 
              }
	 /*   cpufreq_put_policy(policy);*/
	return 0;
}




static unsigned int get_speed(unsigned long speed)
{
	unsigned long tmp;

if (speed > 1000000) {
		tmp = speed % 10000;
		if (tmp >= 5000)
			speed += 10000;
		/*printf ("%u.%02u GHz", ((unsigned int) speed/1000000),
			((unsigned int) (speed%1000000)/10000));*/
	} else
           if (speed > 100000) {
		tmp = speed % 1000;
		if (tmp >= 500)
			speed += 1000;
		/*printf ("%u MHz", ((unsigned int) speed / 1000));*/
	} else if (speed > 1000) {
		tmp = speed % 100;
		if (tmp >= 50)
			speed += 100;
		/*printf ("%u.%01u MHz", ((unsigned int) speed/1000),
			((unsigned int) (speed%1000)/100));*/
	} else
		/*printf ("%lu kHz", speed);*/

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

void print_status(heartbeat_record_t *current, int wait_for, unsigned long freq, char action, int cores)
{
	printf("%lld\t%.3f\t%u\t%d\t%c\t%d\n", current->beat, current->window_rate, freq, cores, action, wait_for);
}

/**
       * 
       */
int main(int argc, char** argv) {
  int n = 0;
  int i;
  const int MAX = atoi(argv[1]);
  const int CORES = atoi(argv[2]);
  int ncpus=0;

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
        int current_counter =0;
        int i_next =0;
        int check =0;
        int check_next =0;
        int go_next=0;
        int u_next=0;

	setlinebuf(stdout);

/*get the cpus the frequency scaler can act on*/

   ncpus=get_cpus();


 if(!CORES){
           CORES == ncpus;
           }


  else if (CORES > ncpus){
                     printf("Given number of cores is greater than those actually available in the system!\n");
                     exit(2);
                          }
/*get the hardware limits of cpu*/
  ret= get_hardware_limits(cpu);

  min_available_freq = min;

  max_available_freq = max;

/*set the policy for the cores */

  ret= set_policy(CORES);

/*get the available frequencies and store them to a local array*/

  freqs = cpufreq_get_available_frequencies(0);

 if (!freqs){
                     printf("Frequency can not be tuned on this system!\n");
                     exit(2);
                  }

 max_freq = get_available_freqs_size(freqs);


  unsigned long* available_freqs = (unsigned long *) malloc(max_freq *sizeof(unsigned long));


  ret = store_available_freqs( freqs, available_freqs, max_freq);

  

  if (ret!=0){
               printf("Frequency can not be tuned on this system!\n");
                     exit(2);
                }
/*get the current frequency of the system as the inital frequency*/

  current_counter = get_init_frequency(available_freqs, cpu);
 
 if (current_counter >  min_available_freq){

    i_next=1;

   }


  current_freq = cpufreq_get_freq_kernel(cpu);


/*start with heartbeats*/

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
  if (n>1) {
     printf("too many apps!!!!!!!!!!!!!!\n");
     exit(2);
  }

/*  sleep(5);*/

#if 1
  int rc = heart_rate_monitor_init(&heart, apps[0]);

  if (rc != 0)
    printf("Error attaching memory\n");

  printf("buffer depth is %lld\n", (long long int) heart.state->buffer_depth);

  i = 0;
      printf(" rate interval is %f - %f\n", hrm_get_min_rate(&heart), hrm_get_max_rate(&heart));


printf("beat\trate\tfreq\tcores\ttact\twait\n");

  int64_t window_size =  hrm_get_window_size(&heart);
  int wait_for = (int) window_size;
  int current_beat = 0;
  int current_beat_prev= 0;
  int nprocs = 1; 
  int current_core = 0;
  unsigned int set_freq = min;
 

  // return 1;  
    
  while(current_beat < MAX) {
    int rc = -1;
    heartbeat_record_t record;
    


      while (rc != 0 || record.window_rate == 0.0000 ){
	rc = hrm_get_current(&heart, &record);
	current_beat = record.beat;
      }
        
       
      if(current_beat_prev == current_beat)
      continue;

  
      if( current_beat < wait_for){
           current_beat_prev= current_beat;
           current_freq = cpufreq_get_freq_kernel(current_core);
	   print_status(&record, wait_for, current_freq, '.',current_core);
        continue;
      }


   

       /*Situation where frequency is up-scaled*/
      if(record.window_rate < hrm_get_min_rate(&heart)) {  
      
           if (u_next==1){
               
                go_next=1;
                   }
 


  	wait_for = current_beat + window_size;      
         if (current_counter > 0){

                 current_counter--;  
                 cpufreq_set_frequency(current_core, available_freqs[current_counter]);
                 current_freq = cpufreq_get_freq_kernel(current_core);
	         print_status(&record, wait_for, current_freq, '+', current_core);
                 

                 if(current_counter==0){
                              u_next=1;
                       
                            }
             }

       else if (go_next == 1 && current_core < CORES){
              u_next=0;
              go_next=0;
             
              current_core++;
 
             current_counter = get_init_frequency(available_freqs, current_core);
            
                         if(current_counter > 0){
                               current_counter--;  
                               cpufreq_set_frequency(current_core, available_freqs[current_counter]);
                             }

             current_freq = cpufreq_get_freq_kernel(current_core);
             print_status(&record, wait_for, current_freq, 'N', current_core);
         
         }
         

        else{
            current_freq = cpufreq_get_freq_kernel(current_core);
          
            print_status(&record, wait_for, current_freq, 'M', current_core);
       }

      }


     /*Situation where frequency is downscaled*/

      else if(record.window_rate > hrm_get_max_rate(&heart)) {
	wait_for = current_beat + window_size; 
         
          /*   if(next==1){
               check_next=1;
                }*/


            if (current_counter < max_freq -1){

                      current_counter++;
                      cpufreq_set_frequency(current_core, available_freqs[current_counter]);
                      current_freq = cpufreq_get_freq_kernel(current_core);
	              print_status(&record, wait_for, current_freq, '-', current_core);

                      if(current_core > 0){
                    
                         i_next==0;
                        }
                   /*    else if(current_core ==0 && i_next==1){
                        next=1;
                         }*/
                   }
	

           else if ( current_counter > max_freq && current_core > 0)  {

             current_core--;

             current_counter = get_init_frequency(available_freqs, current_core);
             
                 if(current_counter < max_freq -1) {
                           current_counter++;
                           cpufreq_set_frequency(current_core, available_freqs[current_counter]);
                        }
          
            current_freq = cpufreq_get_freq_kernel(current_core);
            print_status(&record, wait_for, current_freq, 'n', current_core);

            }

          
     /*   else if ( current_counter >= max_freq && i_next == 1) {

               current_core++;
               current_counter = get_init_frequency(available_freqs, current_core);

              if(current_counter < max_freq) {
                           current_counter++;
                           cpufreq_set_frequency(current_core, available_freqs[current_counter]);
                        }

               current_freq = cpufreq_get_freq_kernel(current_core);
               print_status(&record, wait_for, current_freq, 'n', current_core);

             if (current_core == CORES){
                      i_next=0;
                     }

            }*/


          else{
             
             current_freq = cpufreq_get_freq_kernel(current_core);
             print_status(&record, wait_for, current_freq, 'm', current_core);
             }

      }


      else {
	wait_for = current_beat+1;
	print_status(&record, wait_for, current_freq, '=', current_core);
      }

      current_beat_prev= current_beat;
      records[i].tag = current_beat;
      records[i].rate = record.window_rate;
      i++;
   

  }

 
/*  for(i = 0; i < MAX; i++) {
    printf("%d, %f\n", records[i].tag, records[i].rate);
  }*/
  heart_rate_monitor_finish(&heart);
#endif

  return 0;
}
