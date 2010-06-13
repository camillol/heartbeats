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

/* square-and-multiply, lol */
int ipow(int base, int exp)
{
	int result = 1;
	while (exp) {
		if (exp & 0x01) result *= base;
		exp >>= 1;
		base *= base;
	}
	return result;
}

#define SPEED_IDX 0
#define POWER_IDX 1
#define CORE_IDX(core) ((core) + 2)
#define STATE_LEN(core_count) (CORE_IDX(core_count))
#define STATE_SIZE(core_count) (sizeof(unsigned long) * STATE_LEN(core_count))

/* completely made up! */
static void calculate_state_properties(unsigned long *state, int core_count)
{
	unsigned long speed = 0, power = 0;
	int i;
	
	for (i = 0; i < core_count; i++) {
		speed += state[CORE_IDX(i)] / 1000;
		power += (state[CORE_IDX(i)] > 0 ? 1000 : 0) + state[CORE_IDX(i)] / 1000;
	}
	state[SPEED_IDX] = speed;
	state[POWER_IDX] = power;
}

static void generate_machine_states_internal(unsigned long **state_cursor, unsigned long *state, int core_count, int freq_count, unsigned long *freq_array, int core)
{
	int i;
	for (i = 0; i <= freq_count; i++) {
		state[CORE_IDX(core)] = (i == freq_count ? 0 : freq_array[i]);
		if (core == core_count - 1) {
			calculate_state_properties(state, core_count);
			memcpy(*state_cursor, state, STATE_SIZE(core_count));
			(*state_cursor) += STATE_LEN(core_count);
		} else generate_machine_states_internal(state_cursor, state, core_count, freq_count, freq_array, core + 1);
	}
}

unsigned long *create_machine_states(int *state_count, int core_count, int freq_count, unsigned long *freq_array)
{
	unsigned long *states, *state, *statecursor;
	
	*state_count = ipow(freq_count + 1, core_count);	/* we add freq 0 to represent the core being off */
	state = malloc(STATE_SIZE(core_count));
	statecursor = states = malloc(STATE_SIZE(core_count) * *state_count);
	generate_machine_states_internal(&statecursor, state, core_count, freq_count, freq_array, 0);
	free(state);
	return states;
}

int compare_states_on_speed(unsigned long *a, unsigned long *b)
{
	if (a[SPEED_IDX] < b[SPEED_IDX]) return -1;
	if (a[SPEED_IDX] > b[SPEED_IDX]) return 1;
	/* if the speed is the same, sort by decreasing power */
	if (a[POWER_IDX] > b[POWER_IDX]) return -1;
	if (a[POWER_IDX] < b[POWER_IDX]) return 1;
	return 0;
}

/* eliminate permutations by requiring that frequencies be monotonically decreasing */
static int redundant_state(unsigned long *state, int core_count)
{
	int j;
	unsigned long last = ULONG_MAX;

	for (j = 0; j < core_count; j++) {
		if (state[CORE_IDX(j)] > last) return 1;
		last = state[CORE_IDX(j)];
	}
	return 0;
}

/* filter Pareto optimal subset */
/*	a point is a Pareto improvement over another if it is better for at least one objective and not worse for any others
	a point is Pareto-optimal if there are no points within the region described by equations x >= x0, y >= y0, z >= z0...
	except for the point (x0,y0,z0...) itself. */
/* we assume that the list is sorted by speed */

static int pareto_optimal(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count)
{
	int i;
	unsigned long *other;
	
	/* the immediately preceding states may have equal speed but lower power */
	for (i = state_index - 1, other = state - STATE_LEN(core_count); i > 0 && other[SPEED_IDX] >= state[SPEED_IDX]; i--, other -= STATE_LEN(core_count)) {
		if (other[POWER_IDX] < state[POWER_IDX]) return 0;
	}
	/* the following states have equal or higher speed */
	for (i = state_index + 1, other = state + STATE_LEN(core_count); i < state_count; i++, other += STATE_LEN(core_count)) {
		if (other[POWER_IDX] < state[POWER_IDX]) return 0;
	}
	return 1;
}

/* breaks ties between equivalent states by picking the first state (which is also the most unbalanced - good for program with poor parallelism) */
static int drop_equivalent(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count)
{
	int i;
	unsigned long *other;

	for (i = state_index - 1, other = state - STATE_LEN(core_count); i > 0 && other[SPEED_IDX] >= state[SPEED_IDX]; i--, other -= STATE_LEN(core_count)) {
		if (other[POWER_IDX] == state[POWER_IDX]) return 1;
	}
	return 0;
}

static unsigned long *read_states_file(char *name, int *state_count, int *core_count)
{
	FILE *fp = NULL;
	char buf[512];
	char *line, *token;
	int n, j;
	unsigned long *states, *state, *state_cursor;
	
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
		fscanf(fp, "\n");
		
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
		if (skip_unoptimal && !pareto_optimal(state, i, states, state_count, core_count))
			continue;
		if (skip_equivalent && drop_equivalent(state, i, states, state_count, core_count))
			continue;

		printf("%lu\t%lu", state[SPEED_IDX], state[POWER_IDX]);
		for (j = 0; j < core_count; j++)
			printf("\t%lu", state[CORE_IDX(j)]);
		printf("\n");
	}
	
	return 0;
}
