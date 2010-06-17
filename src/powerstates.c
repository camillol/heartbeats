/*
 *  powerstates.c
 *  heartbeats
 *
 *  Created by Camillo Lugaresi on 10/06/10.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <limits.h>
#include <unistd.h>
#include <cpufreq.h>

#include "machine_states.h"

#define fail_if(exp, msg) do { if ((exp)) { fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, (msg), strerror(errno)); goto fail; } } while (0)

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

static unsigned long *read_states_file(char *name, int *state_count, int *core_count)
{
	FILE *fp = NULL;
	char buf[512];
	char *line, *token;
	int n, j;
	unsigned long *states = NULL, *state = NULL, *state_cursor;
	
	fp = fopen(name, "r");
	fail_if(!fp, "could not open file");
	
	line = fgets(buf, sizeof(buf), fp);
	fail_if(!line, "wrong format");
	while ((token = strsep(&line, " \t")) != NULL)
		if (sscanf(token, "core%d", &n) >= 1)
			*core_count = n + 1;
	fail_if(*core_count < 1, "wrong format");
	
	n = 1000;
	state = malloc(STATE_SIZE(*core_count));
	state_cursor = states = malloc(STATE_SIZE(*core_count) * n);
	*state_count = 0;
	
	while (1) {
		if (fscanf(fp, "%lu\t%lu", &state[SPEED_IDX], &state[POWER_IDX]) < 2) goto end;
		for (j = 0; j < *core_count; j++)
			if (fscanf(fp, "\t%lu", &state[CORE_IDX(j)]) < 1) goto end;
		if (getc(fp) != '\n') goto end;
		
		if (*state_count >= n) {
			n *= 2;
			states = realloc(states, STATE_SIZE(*core_count) * n);
			state_cursor = states + STATE_LEN(*core_count) * *state_count;
		}
		(*state_count)++;
		memcpy(state_cursor, state, STATE_SIZE(*core_count));
		state_cursor += STATE_LEN(*core_count);
	}
end:
	if (fp) fclose(fp);
	if (state) free(state);
	return states;
fail:
	if (states) free(states);
	states = NULL;
	goto end;
}

int main(int argc, char **argv)
{
	struct cpufreq_available_frequencies *freq_list;
	int core_count;
	int i, j;
	unsigned long *states, *state;
	int state_count;

	int opt;
	int skip_redundant = 0;
	int skip_unoptimal = 0;
	char *state_file_name = NULL;
	int skip_equivalent = 0;
	
	while ((opt = getopt(argc, argv, "rpf:u")) != -1) switch (opt) {
	case 'r':
		skip_redundant = 1;
		break;
	case 'p':
		skip_unoptimal = 1;
		break;
	case 'f':
		state_file_name = optarg;
		break;
	case 'u':
		skip_equivalent = 1;
		break;
	default:
		fprintf(stderr, "Usage: %s [-r] [-p] [-f file] [-u]\n", argv[0]);
		exit(1);
	}
	
	if (state_file_name) {
		states = read_states_file(state_file_name, &state_count, &core_count);
	} else {
		int freq_count;
		unsigned long *freq_array;
		
		core_count = get_core_count();
		freq_list = cpufreq_get_available_frequencies(0);
		freq_count = create_freq_array(freq_list, &freq_array);
		
		states = create_machine_states(&state_count, core_count, freq_count, freq_array);
	}
	qsort(states, state_count, STATE_SIZE(core_count), compare_states_on_speed);

	printf("speed\tpower");
	for (j = 0; j < core_count; j++)
		printf("\tcore%d", j);
	printf("\n");
	
	for (i = 0, state = states; i < state_count; i++, state+=STATE_LEN(core_count)) {
		if (skip_redundant && redundant_state(state, core_count))
			continue;
		if (skip_equivalent && drop_equivalent(state, i, states, state_count, core_count))
			continue;
		if (skip_unoptimal && !pareto_optimal(state, i, states, state_count, core_count))
			continue;

		printf("%lu\t%lu", state[SPEED_IDX], state[POWER_IDX]);
		for (j = 0; j < core_count; j++)
			printf("\t%lu", state[CORE_IDX(j)]);
		printf("\n");
	}
	
	return 0;
}
