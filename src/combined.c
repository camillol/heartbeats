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
#include <limits.h>
#include "heart_rate_monitor.h"

#include "machine_states.h"

/*
 The best part of C is macros. The second best part of C is goto.
 */
#define fail_if(exp, msg) do { if ((exp)) { fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, (msg), strerror(errno)); goto fail; } } while (0)

#define ACTUATOR_CORE_COUNT 1
#define ACTUATOR_GLOBAL_FREQ 2
#define ACTUATOR_SINGLE_FREQ 3
#define ACTUATOR_MACHINE_SPD 4

#define DEBUG 0

/* just my type */

typedef struct actuator actuator_t;
struct actuator {
	int id;
	pid_t pid;
	int core;
	int (*init_f) (actuator_t *act);
	int (*action_f) (actuator_t *act);
	int64_t value;
	int64_t set_value;
	int64_t min;
	int64_t max;
	void *data;
};

typedef void (*decision_function_t) (heartbeat_record_t *hb, int act_count, actuator_t *acts);

typedef struct freq_scaler_data {
	unsigned long *freq_array;
	int freq_count;
	int cur_index;
} freq_scaler_data_t;

typedef struct machine_state_data {
	unsigned long *states;
	int state_count;
	actuator_t *core_act;
	actuator_t *freq_acts[16];
	unsigned long *scratch_state;
} machine_state_data_t;

/* a global is fine too */

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

void get_actuators(actuator_t **core_act, actuator_t **global_freq_act, int max_single_freq_acts, actuator_t **single_freq_acts, actuator_t **speed_act)
{
	int i;
	extern int actuator_count;
	extern actuator_t *controls;
	
	for (i = 0; i < actuator_count; i++) {
		if (controls[i].id == ACTUATOR_CORE_COUNT && core_act)
			*core_act = &controls[i];
		else if (controls[i].id == ACTUATOR_GLOBAL_FREQ && global_freq_act)
			*global_freq_act = &controls[i];
		else if (controls[i].id == ACTUATOR_SINGLE_FREQ && single_freq_acts && controls[i].core <= max_single_freq_acts)
			single_freq_acts[controls[i].core] = &controls[i];
		else if (controls[i].id == ACTUATOR_MACHINE_SPD && speed_act)
			*speed_act = &controls[i];
	}
}

/* core allocator stuff */

int get_core_count ()
{
	static int count = 0;
	FILE *fp = NULL;

	if (!count) {
		char buf[256];

		fp = fopen("/proc/cpuinfo", "r");
		fail_if(!fp, "cannot open /proc/cpuinfo");
		while (fgets(buf, sizeof(buf), fp))
			if (strstr(buf, "processor"))
				count++;
		fclose(fp);
fail:
		if (count < 1) count = 1;
	}
	return count;
}

int core_init (actuator_t *act)
{
	char buf[256];
	FILE *proc;
	unsigned int affinity;
	
	snprintf(buf, sizeof(buf), "taskset -p %d | sed 's/.* //'", (int)act->pid);
	proc = popen(buf, "r");
	fail_if(!proc, "cannot read initial processor affinity");
	fail_if(fscanf(proc, "%x", &affinity) < 1, "cannot parse initial processor affinity");
	pclose(proc);
	
	act->value = 0;
	while (affinity) {
		act->value += affinity & 0x01;
		affinity /= 2;
	}
	act->set_value = act->value;
	act->min = 1;
	act->max = get_core_count();

	return 0;
fail:
	return -1;
}

int core_act (actuator_t *act)
{
	char command[256];
	int err;
	
	snprintf(command, sizeof(command), "taskset -pc 0-%d %d > /dev/null", (int)(act->set_value - 1), (int)act->pid);
	err = system(command);
	if (!err)
		act->value = act->set_value;
	return err;
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
	freq = freq_list;
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

int single_freq_init (actuator_t *act)
{
	int err;
	struct cpufreq_policy *policy;
	struct cpufreq_available_frequencies *freq_list;
	freq_scaler_data_t *data;
	unsigned long freq_min, freq_max;
	
	act->data = data = malloc(sizeof(freq_scaler_data_t));
	fail_if(!data, "cannot allocate freq data block");
	
	err = cpufreq_get_hardware_limits(act->core, &freq_min, &freq_max);
	fail_if(err, "cannot get cpufreq hardware limits");
	act->min = freq_min;
	act->max = freq_max;
	
	policy = cpufreq_get_policy(act->core);
	fail_if(!policy, "cannot get cpufreq policy");
	if (strcmp(policy->governor, "userspace") != 0) {
		err = cpufreq_modify_policy_governor(act->core, "userspace");
		policy = cpufreq_get_policy(act->core);
		fail_if (strcmp(policy->governor, "userspace") != 0, "cannot set cpufreq policy to userspace");
	}
	
	freq_list = cpufreq_get_available_frequencies(act->core);
	data->freq_count = create_freq_array(freq_list, &data->freq_array);
	fail_if(data->freq_count < 1, "cannot get frequency list");
	
	act->value = act->set_value = cpufreq_get_freq_kernel(act->core);
	data->cur_index = get_freq_index(data, act->value);
	
	return 0;
fail:
	return -1;
}

int global_freq_init (actuator_t *act)
{
	int err;
	
	act->core = 0;	/* just get all data from the first cpu and assume they're all the same */
	err = single_freq_init(act);
	act->core = -1;
	return err;
}

int single_freq_act (actuator_t *act)
{
	int err = 0;
	
	err = cpufreq_set_frequency(act->core, act->set_value);
	/* warning: cpufreq_set_frequency tries sysfs first, then proc; this means that if
	 sysfs fails with EACCESS, the errno is then masked by the ENOENT from proc! */
	act->value = cpufreq_get_freq_kernel(act->core);
	return err;
}

int global_freq_act (actuator_t *act)
{
	int err = 0;
	int cpu;
	
	for (cpu = 0; cpu < get_core_count(); cpu++)
		err = err || cpufreq_set_frequency(cpu, act->set_value);
	/* warning: cpufreq_set_frequency tries sysfs first, then proc; this means that if
	sysfs fails with EACCESS, the errno is then masked by the ENOENT from proc! */
	act->value = cpufreq_get_freq_kernel(0);
	return err;
}

/* machine speed actuator */

unsigned long get_current_speed(actuator_t *act)
{
	machine_state_data_t *data = act->data;
	unsigned long *current_state = data->scratch_state;
	int i, core_count = get_core_count();

	for (i = 0; i < core_count; i++)
		current_state[CORE_IDX(i)] = i < data->core_act->value ? data->freq_acts[i]->value : 0;
	calculate_state_properties(current_state, core_count);
#if DEBUG
			int j;
			printf("%lu\t%lu", current_state[SPEED_IDX], current_state[POWER_IDX]);
			for (j = 0; j < core_count; j++)
				printf("\t%lu", current_state[CORE_IDX(j)]);
			printf("\n");
#endif
	return current_state[SPEED_IDX];
}

int machine_speed_init (actuator_t *act)
{
	machine_state_data_t *data;
	unsigned long *states, *all_states;
	unsigned long *in_state, *out_state;
	int state_count, filtered_count;
	
	int core_count, i;
	freq_scaler_data_t *freq_data;

	act->data = data = malloc(sizeof(machine_state_data_t));
	fail_if(!data, "cannot allocate powerstate data block");

	get_actuators(&data->core_act, NULL, 16, &data->freq_acts[0], NULL);
	fail_if(data->core_act->max > 16, "too many cores lol");
	freq_data = data->freq_acts[0]->data;
	core_count = get_core_count();
	
	all_states = create_machine_states(&state_count, core_count, freq_data->freq_count, freq_data->freq_array);
	fail_if(!all_states, "cannot generate machine states");

	qsort(all_states, state_count, STATE_SIZE(core_count), compare_states_on_speed);
	states = malloc(STATE_SIZE(core_count) * state_count);
	for (i = 0, in_state = all_states, out_state = states, filtered_count = 0; i < state_count; i++, in_state+=STATE_LEN(core_count)) {
		if (!redundant_state(in_state, core_count) &&
			!drop_equivalent(in_state, i, all_states, state_count, core_count) &&
			pareto_optimal(in_state, i, all_states, state_count, core_count) &&
			in_state[SPEED_IDX] > 0)
		{
#if DEBUG
			int j;
			printf("%lu\t%lu", in_state[SPEED_IDX], in_state[POWER_IDX]);
			for (j = 0; j < core_count; j++)
				printf("\t%lu", in_state[CORE_IDX(j)]);
			printf("\n");
#endif
			memmove (out_state, in_state, STATE_SIZE(core_count));
			out_state += STATE_LEN(core_count);
			filtered_count++;
		}
	}
	data->state_count = state_count = filtered_count;
	data->states = states = realloc(states, STATE_SIZE(core_count) * state_count);
	free(all_states);
	
	act->min = STATE_I(states, core_count, 0)[SPEED_IDX];
	act->max = STATE_I(states, core_count, state_count-1)[SPEED_IDX];
	
	data->scratch_state = malloc(STATE_SIZE(core_count));
	act->value = act->set_value = get_current_speed(act);
	
	return 0;
fail:
	return -1;
}

int machine_speed_act (actuator_t *act)
{
	machine_state_data_t *data = act->data;
	unsigned long *states = data->states;
	int core_count = data->core_act->max;
	int f = 0, t = data->state_count - 1, i;
	unsigned long *state, *state2 = NULL;
	int d;
	
	/* maybe we should check around the current state first? oh who cares */

	/* binary search FTW */
	do {
		i = (t+f)/2;
		state = STATE_I(states, core_count, i);
		if (state[SPEED_IDX] == act->set_value) break;
		else if (state[SPEED_IDX] < act->set_value) f = i + 1;
		else if (state[SPEED_IDX] > act->set_value) t = i - 1;
	} while (f < t);
	
	/* if it's not an exact match, maybe there's a closer one on the other side */
	d = state[SPEED_IDX] - act->set_value;
	if (d > 0) {
		if (i > 0) state2 = STATE_I(states, core_count, i-1);
	} else if (d < 0) {
		if (i < data->state_count - 1) state2 = STATE_I(states, core_count, i+1);
	}
	if (state2 && abs(state2[SPEED_IDX] - act->set_value) < abs(d)) state = state2;
	
	/* now let's implement it */
	for (i = 0; i < core_count && state[CORE_IDX(i)] > 0; i++)
		data->freq_acts[i]->set_value = state[CORE_IDX(i)];
	data->core_act->set_value = i;
#if DEBUG
	if (i < 1) {
		printf("NNNNOOOOOOOOOO\n");
	}
#endif
	return 0;
}

/* decision functions */

void dummy_control (heartbeat_record_t *hb, int act_count, actuator_t *acts)
{
	/* do nothing, lol */
}

void core_heuristics (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	static actuator_t *core_act = NULL;

	if (!core_act) get_actuators(&core_act, NULL, 0, NULL, NULL);
	
	if (current->window_rate < hrm_get_min_rate(&hrm)) {
		if (core_act->value < core_act->max) core_act->set_value++;
	}
	else if(current->window_rate > hrm_get_max_rate(&hrm)) {
		if (core_act->value > core_act->min) core_act->set_value--;
	}
}

void freq_heuristics (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	static actuator_t *freq_act = NULL;
	freq_scaler_data_t *freq_data;

	if (!freq_act) get_actuators(NULL, &freq_act, 0, NULL, NULL);
	freq_data = freq_act->data;
	
	if (current->window_rate < hrm_get_min_rate(&hrm)) {
		if (freq_data->cur_index > 0) {
			freq_data->cur_index--;
			freq_act->set_value = freq_data->freq_array[freq_data->cur_index];
		}
	}
	else if(current->window_rate > hrm_get_max_rate(&hrm)) {
		if (freq_data->cur_index < freq_data->freq_count-1) {
			freq_data->cur_index++;
			freq_act->set_value = freq_data->freq_array[freq_data->cur_index];
		}
	}
}

void uncoordinated_heuristics (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	core_heuristics(current, act_count, acts);
	freq_heuristics(current, act_count, acts);
}

void step_heuristics (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	static actuator_t *core_act = NULL;
	static actuator_t *freq_acts[16];
	int last_core;
	freq_scaler_data_t *freq_data;	

	if (!core_act) {
		get_actuators(&core_act, NULL, 16, &freq_acts[0], NULL);
		if (core_act->max > 16) exit(2);
	}
	
	last_core = core_act->value - 1;
	freq_data = freq_acts[last_core]->data;
	
	if (current->window_rate < hrm_get_min_rate(&hrm)) {
		if (freq_data->cur_index > 0) {
			/* increase last core's frequency if possible */
			freq_data->cur_index--;
			freq_acts[last_core]->set_value = freq_data->freq_array[freq_data->cur_index];
		} else if (last_core < core_act->max - 1) {
			/* else, add another core... */
			core_act->set_value = core_act->value + 1;
			last_core++;
			/* ...at the lowest initial frequency */
			freq_data = freq_acts[last_core]->data;
			freq_data->cur_index = freq_data->freq_count-1;
			freq_acts[last_core]->set_value = freq_data->freq_array[freq_data->cur_index];
		}
	}
	else if(current->window_rate > hrm_get_max_rate(&hrm)) {
		if (freq_data->cur_index < freq_data->freq_count-1) {
			/* decrease last core's frequency if possible */
			freq_data->cur_index++;
			freq_acts[last_core]->set_value = freq_data->freq_array[freq_data->cur_index];
		} else if (last_core > core_act->min - 1) {
			/* else, reduce core count */
			core_act->set_value = core_act->value - 1;
			last_core--;
			/* the core that is now last should already be at max frequency */
		}
	}
}

/*
 P controller:
	e = sp - y
	u = [uo +] Kp*e
 
 PI controller:
	u = [uo +] Kp*e + Ki*sum(e)
 
 pseudo PI controller:
	u = [u0 +] ... TODO
 */


void core_controller (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	static actuator_t *core_act = NULL;
	
	double target_rate = (hrm_get_max_rate(&hrm) + hrm_get_min_rate(&hrm)) / 2.0;
	double error = target_rate - current->window_rate;
	double Kp = 0.4;
	
	if (!core_act) get_actuators(&core_act, NULL, 0, NULL, NULL);
	core_act->set_value = core_act->value + Kp*error;
	if (core_act->set_value < core_act->min) core_act->set_value = core_act->min;
	else if (core_act->set_value > core_act->max) core_act->set_value = core_act->max;
}

void machine_state_controller (heartbeat_record_t *current, int act_count, actuator_t *acts)
{
	static actuator_t *speed_act = NULL;
	
	double target_rate = (hrm_get_max_rate(&hrm) + hrm_get_min_rate(&hrm)) / 2.0;
	double error = target_rate - current->window_rate;
	double Kp = 100;
	
	if (!speed_act) get_actuators(NULL, NULL, 0, NULL, &speed_act);
	speed_act->set_value = speed_act->value + Kp*error;
#if DEBUG
	printf("target: %f hr: %f error: %f speed: %d -> %d ", target_rate, current->window_rate, error, speed_act->value, speed_act->set_value);
#endif
	if (speed_act->set_value < speed_act->min) speed_act->set_value = speed_act->min;
	else if (speed_act->set_value > speed_act->max) speed_act->set_value = speed_act->max;
#if DEBUG
	printf("clipped: %d\n", speed_act->set_value);
#endif
}

/* BACK TO ZA CHOPPA */

void print_status(heartbeat_record_t *current, int64_t skip_until_beat, char action, int act_count, actuator_t *controls)
{
	int i;

	printf("%lld\t%.3f\t%lld\t%c", (long long int)current->beat, current->window_rate, (long long int)skip_until_beat, action);
	for (i = 0; i < act_count; i++)
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
	int core_count;
	int opt;
	int max_beats = INT_MAX;
	
	extern int actuator_count;
	extern actuator_t *controls;
	actuator_t *next_ctl;
	
	decision_function_t decision_f = machine_state_controller;
	int acted;

	/* we want to see this in realtime even when it's piped through tee */
	setlinebuf(stdout);
	
	/* getting rich with stock options */	
	while ((opt = getopt(argc, argv, "d:")) != -1) switch (opt) {
		case 'd':
			if (strcmp(optarg, "core_heuristics") == 0) decision_f = core_heuristics;
			else if (strcmp(optarg, "freq_heuristics") == 0) decision_f = freq_heuristics;
			else if (strcmp(optarg, "uncoordinated_heuristics") == 0) decision_f = uncoordinated_heuristics;
			else if (strcmp(optarg, "step_heuristics") == 0) decision_f = step_heuristics;
			else if (strcmp(optarg, "core_controller") == 0) decision_f = core_controller;
			else if (strcmp(optarg, "machine_state_controller") == 0) decision_f = machine_state_controller;
			else {
				fprintf(stderr, "%s: unknown decision function\n", argv[0]);
				exit(1);
			}
			break;
		default:
			fprintf(stderr, "Usage: %s [-d decision_function]\n", argv[0]);
			exit(1);
	}	
	argc -= optind;
	argv += optind;	
	if (argc > 1)
		max_beats = argv[1];
	
	/* setupping arbit */
	heartbeat_dir = getenv("HEARTBEAT_ENABLED_DIR");
	fail_if(heartbeat_dir == NULL, "environment variable HEARTBEAT_ENABLED_DIR undefined");
	
	while (n_apps == 0)
		n_apps = get_heartbeat_apps(apps, sizeof(apps)/sizeof(apps[0]));
	fail_if(n_apps != 1, "this service only supports a single app. please delete c:\\system32");
	printf("monitoring process %d\n", apps[0]);
	
	/* initrogenizing old river control structure */
	core_count = get_core_count();
	actuator_count = core_count + 3;
	
	controls = malloc(sizeof(actuator_t) * actuator_count);
	fail_if(!controls, "could not allocate actuators");
	/* PROBLEM!!!!! the machine speed actuator needs to init last, but act first! WHAT NOW */
	/* create the list in action order, but init in special order... QUICK AND DIRTY = OPTIMAL */
	next_ctl = controls;
	*next_ctl++ =     (actuator_t) { .id = ACTUATOR_MACHINE_SPD, .core = -1, .pid = apps[0], .init_f = machine_speed_init, .action_f = machine_speed_act };
	for (i = 0; i < core_count; i++)
		*next_ctl++ = (actuator_t) { .id = ACTUATOR_SINGLE_FREQ, .core = i,  .pid = -1,      .init_f = single_freq_init,   .action_f = single_freq_act };
	*next_ctl++ =     (actuator_t) { .id = ACTUATOR_GLOBAL_FREQ, .core = -1, .pid = -1,      .init_f = global_freq_init,   .action_f = global_freq_act };
	*next_ctl++ =     (actuator_t) { .id = ACTUATOR_CORE_COUNT,  .core = -1, .pid = apps[0], .init_f = core_init,          .action_f = core_act };
	
	
	for (i = 1; i < actuator_count; i++) {
		err = controls[i].init_f(&controls[i]);
		fail_if(err, "cannot initialize actuator");
	}
	/* initialize machine speed actuator last! */
	err = controls[0].init_f(&controls[0]);
	fail_if(err, "cannot initialize actuator");
	
	/* begin monitoration of lone protoss */
	err = heart_rate_monitor_init(&hrm, apps[0]);
	fail_if(err, "cannot start heart rate monitor");
	
	window_size = hrm_get_window_size(&hrm);
	current.beat = -1;
	
	do {
		do {
			err = hrm_get_current(&hrm, &current);
		} while (err || current.beat <= last_beat || current.window_rate == 0.0);

		last_beat = current.beat;
		if (current.beat < skip_until_beat) {
			print_status(&current, skip_until_beat, '.', actuator_count, controls);
			continue;
		}
		
		/*printf("Current beat: %lld, tag: %d, window: %lld, window_rate: %f\n",
			   current.beat, current.tag, window_size, current.window_rate);*/
		
		decision_f(&current, actuator_count, controls);
		
		acted = 0;
		for (i = 0; i < actuator_count; i++) {
			actuator_t *act = &controls[i];
			if (act->set_value != act->value) {
#if DEBUG
				printf("act %d: %d -> %d\n", i, act->value, act->set_value);
#endif
				err = act->action_f(act);	/* TODO: handle error */
				if (err) fprintf(stderr, "action %d failed: %s\n", act->id, strerror(errno));
				acted = 1;
			}
		}
		/* this is horrible but necessary */
		controls[0].value = get_current_speed(&controls[0]);

		skip_until_beat = current.beat + (acted ? window_size : 1);
		
		print_status(&current, skip_until_beat, acted ? '*' : '=', actuator_count, controls);
	} while (current.beat < max_beats);
	
	heart_rate_monitor_finish(&hrm);
	
	return 0;
fail:
	return 1;
}

/* here are some globals without lexical scoping, just to mix things up! */
int actuator_count;
actuator_t *controls;

