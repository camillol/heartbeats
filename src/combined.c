/*
 *  combined.c
 *  
 *
 *  Created by Camillo Lugaresi on 04/06/10.
 *
 */

#include <sys/errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <cpufreq.h>
#include "heart_rate_monitor.h"

/*
 The best part of C is macros. The second best part of C is goto.
 */
#define fail_if(exp, msg) do { if ((exp)) { fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, (msg), strerror(errno)); goto fail; } } while (0)

#define ACTUATOR_CORE 1
#define ACTUATOR_FREQ 2
#define ACTUATOR_COUNT 2

typedef struct actuator actuator_t;
struct actuator {
	int id;
	pid_t pid;
	int (*init_f) (actuator_t *act);
	int (*action_f) (actuator_t *act);
	uint64_t value;
	uint64_t set_value;
	void *data;
};

typedef void (*decision_function_t) (heartbeat_record_t *hb, int act_count, actuator_t *acts);

typedef struct freq_scaler_data {
	unsigned long *freq_array;
	int freq_count;
	int cur_index;
	unsigned long freq_min;
	unsigned long freq_max;
} freq_scaler_data_t;

heart_rate_monitor_t hrm;
char *heartbeat_dir;

/* this is way simpler than spawning a process! */
int get_heartbeat_apps(int *pids, int maxcount)
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;
	int pid;
	char *end;
	
	dir = opendir(heartbeat_dir);
	fail_if(dir == NULL, "cannot open heartbeat dir");
	while (((entry = readdir(dir)) != NULL) && count < maxcount) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		pid = strtol(entry->d_name, &end, 10);
		if (*end == 0) pids[count++] = pid;
		else fprintf(stderr, "file name is not a pid: %s\n", entry->d_name);
	}
	(void)closedir(dir);
	return count;
fail:
	return -1;
}

/* core allocator stuff */

int core_init (actuator_t *act)
{
	char buf[256];
	FILE *proc;
	unsigned int affinity;
	
	snprintf(buf, sizeof(buf), "taskset -p %d | sed 's/.* //'", (int)act->pid);
	proc = popen(buf, "r");
	fail_if(!proc, "cannot read initial processor affinity");
	fail_if(fscanf("x", &affinity) < 1, "cannot parse initial processor affinity");
	pclose(proc);
	
	act->value = 0;
	while (affinity) {
		act->value += affinity & 0x01;
		affinity /= 2;
	}
	act->set_value = act->value;
	return 0;
fail:
	return -1;
}

int core_act (actuator_t *act)
{
	char command[256];
	
	snprintf(command, sizeof(command), "taskset -pc 0-%d %d > /dev/null", (int)(act->value - 1), (int)act->pid);
	return system(command);
}

/* frequency scaler stuff */

int create_freq_array(struct cpufreq_available_frequencies *freq_list, unsigned long **freq_array_p)
{
	struct cpufreq_available_frequencies *freq = freq_list;
	int n = 0;
	unsigned long *f;
	
	while (freq) {
		n++;
		freq = freq->next;
	}
	f = *freq_array_p = (unsigned long *) malloc(n * sizeof(unsigned long));
	fail_if(!f, "cannot allocate freq array");
	while (freq) {
		*f++ = freq->frequency;
		freq = freq->next;
	}
	return n;
fail:
	return -1;
}

int get_freq_index(freq_scaler_data_t *data, unsigned long freq)
{
	int i;
	
	for (i = 0; i < data->freq_count; i++)
		if (data->freq_array[i] == freq)
			return i;
	return -1;
}

int freq_init (actuator_t *act)
{
	int err;
	struct cpufreq_policy *policy;
	struct cpufreq_available_frequencies *freq_list;
	freq_scaler_data_t *data;

	act->data = data = malloc(sizeof(freq_scaler_data_t));
	fail_if(!data, "cannot allocate freq data block");

	err = cpufreq_get_hardware_limits(0, &data->freq_min, &data->freq_max);
	fail_if(err, "cannot get cpufreq hardware limits");
	
	policy = cpufreq_get_policy(0);
	fail_if(!policy, "cannot get cpufreq policy");
	if (strcmp(policy->governor, "userspace") != 0) {
		err = cpufreq_modify_policy_governor(0, "userspace");
		policy = cpufreq_get_policy(0);
		fail_if (strcmp(policy->governor, "userspace") != 0, "cannot set cpufreq policy to userspace");
	}
	
	freq_list = cpufreq_get_available_frequencies(0);
	data->freq_count = create_freq_array(freq_list, &data->freq_array);
	fail_if(data->freq_count < 1, "cannot get frequency list");
	
	act->value = act->set_value = cpufreq_get_freq_kernel(0);
	data->cur_index = get_freq_index(data, act->value);
	
	return 0;
fail:
	return -1;
}

int freq_act (actuator_t *act)
{
	return cpufreq_set_frequency(0, act->set_value);
}

/* decision functions */

void dummy_control (heartbeat_record_t *hb, int act_count, actuator_t *acts)
{
	/* do nothing, lol */
}

/* BACK TO ZA CHOPPA */

void print_status(heartbeat_record_t *current, int64_t skip_until_beat, char action, int act_acount, actuator_t *controls)
{
	int i;

	printf("%lld\t%.3f\t%lld\t%c", (long long int)current->beat, current->window_rate, (long long int)skip_until_beat, action);
	for (i = 0; i < act_acount; i++)
		printf("\t%lld", controls[i].value);
	printf("\n");
}

int main(int argc, char **argv)
{
	int n_apps = 0;
	int apps[16];
	int err;
	int i;
	int64_t window_size;
	int64_t skip_until_beat = 0;
	int64_t last_beat = 0;
	heartbeat_record_t current;
	actuator_t controls[ACTUATOR_COUNT] = {
		{ .id = ACTUATOR_CORE, .init_f = core_init, .action_f = core_act },
		{ .id = ACTUATOR_FREQ, .init_f = freq_init, .action_f = freq_act }
	};
	decision_function_t decision_f;
	int acted;

	/* we want to see this in realtime even when it's piped through tee */
	setlinebuf(stdout);
	
	/* setupping arbit */
	heartbeat_dir = getenv("HEARTBEAT_ENABLED_DIR");
	fail_if(heartbeat_dir == NULL, "environment variable HEARTBEAT_ENABLED_DIR undefined");
	
	while (n_apps == 0)
		n_apps = get_heartbeat_apps(apps, sizeof(apps)/sizeof(apps[0]));
	fail_if(n_apps != 1, "this service only supports a single app. please delete c:\\system32");
	printf("monitoring process %d\n", apps[0]);
	
	/* initrogenizing old river control structure */
	for (i = 0; i < ACTUATOR_COUNT; i++) {
		controls[i].pid = apps[0];
		err = controls[i].init_f(&controls[i], apps[0]);
		fail_if(err, "cannot initialize actuator");
	}
	decision_f = dummy_control;
	
	/* begin monitoration of lone protoss */
	err = heart_rate_monitor_init(&hrm, apps[0]);
	fail_if(err, "cannot start heart rate monitor");
	
	window_size = hrm_get_window_size(&hrm);
	current.beat = -1;
	
	while (1) {	/* what, me worry? */
		do {
			err = hrm_get_current(&hrm, &current);
		} while (err || current.beat <= last_beat);

		last_beat = current.beat;
		if (current.beat < skip_until_beat) {
			print_status(&current, skip_until_beat, '.', ACTUATOR_COUNT, controls);
			continue;
		}
		
		/*printf("Current beat: %lld, tag: %d, window: %lld, window_rate: %f\n",
			   current.beat, current.tag, window_size, current.window_rate);*/
		
		decision_f(&current, ACTUATOR_COUNT, controls);
		
		acted = 0;
		for (i = 0; i < ACTUATOR_COUNT; i++) {
			actuator_t *act = &controls[i];
			if (act->set_value != act->value) {
				err = act->action_f(act, apps[0]);	/* TODO: handle error */
				act->value = act->set_value;
				acted = 1;
			}
		}
		skip_until_beat = current.beat + (acted ? window_size : 1);
		
		print_status(&current, skip_until_beat, acted ? '*' : '=', ACTUATOR_COUNT, controls);
	}
	
	heart_rate_monitor_finish(&hrm);
	
	return 0;
fail:
	return 1;
}
