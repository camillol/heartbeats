/*
 *  machine_states.c
 *  heartbeats
 *
 *  Created by Camillo Lugaresi on 17/06/10.
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

/* square-and-multiply, lol */
static int ipow(int base, int exp)
{
	int result = 1;
	while (exp) {
		if (exp & 0x01) result *= base;
		exp >>= 1;
		base *= base;
	}
	return result;
}

/* completely made up! */
void calculate_state_properties(unsigned long *state, int core_count)
{
	unsigned long speed = 0, power = 0;
	int i;
	
	for (i = 0; i < core_count; i++) {
		speed += state[CORE_IDX(i)];
		power += (state[CORE_IDX(i)] > 0 ? 1000000 : 0) + state[CORE_IDX(i)];
	}
	state[SPEED_IDX] = speed / 10000;
	state[POWER_IDX] = power / 10000;
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

int compare_states_on_speed(const void *a, const void *b)
{
	const unsigned long *sa = a;
	const unsigned long *sb = b;

	if (sa[SPEED_IDX] < sb[SPEED_IDX]) return -1;
	if (sa[SPEED_IDX] > sb[SPEED_IDX]) return 1;
	/* if the speed is the same, sort by decreasing power */
	if (sa[POWER_IDX] > sb[POWER_IDX]) return -1;
	if (sa[POWER_IDX] < sb[POWER_IDX]) return 1;
	return 0;
}

/* eliminate permutations by requiring that frequencies be monotonically decreasing */
int redundant_state(unsigned long *state, int core_count)
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

int pareto_optimal(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count)
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
int drop_equivalent(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count)
{
	int i;
	unsigned long *other;
	
	for (i = state_index - 1, other = state - STATE_LEN(core_count); i > 0 && other[SPEED_IDX] >= state[SPEED_IDX]; i--, other -= STATE_LEN(core_count)) {
		if (other[POWER_IDX] == state[POWER_IDX]) return 1;
	}
	return 0;
}


