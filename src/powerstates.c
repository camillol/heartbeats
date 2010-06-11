/*
 *  powerstates.c
 *  heartbeats
 *
 *  Created by Camillo Lugaresi on 10/06/10.
 *
 */

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
	result = 1;
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
#define STATE_SIZE(core_count) (sizeof(unsigned long) * CORE_IDX(core_count))

/* completely made up! */
static void calculate_state_properties(unsigned long *state, int core_count)
{
	unsigned long speed = 0, power = 0;
	int i;
	
	for (i = 0; i < core_count; i++) {
		speed += state[CORE_IDX(i)];
		power += (state[CORE_IDX(i)] > 0 ? 1000 : 0) + state[CORE_IDX(i)];
	}
	state[SPEED_IDX] = speed;
	state[POWER_IDX] = power;
}

static void generate_machine_states_internal(unsigned long **states, unsigned long *state, int core_count, int freq_count, unsigned long *freq_array, int core)
{
	int i;
	for (i = 0; i <= freq_count; i++) {
		state[CORE_IDX(core)] = (i == freq_count ? 0 : freq_array[i]);
		if (core == core_count - 1) {
			calculate_state_properties(state);
			memcpy(*states++, state, STATE_SIZE(core_count));
		} else generate_machine_states_internal(states, state, core_count, freq_count, freq_array, core + 1);
	}
}

unsigned long *create_machine_states(int *state_count, int core_count, int freq_count, unsigned long *freq_array)
{
	unsigned long *states, *state;
	
	*state_count = ipow(freq_count, core_count)
	state = malloc(STATE_SIZE(core_count));
	states = malloc(STATE_SIZE(core_count) * *state_count);
	generate_machine_states_internal(states, state, core_count, freq_count, freq_array, 0);
	free(state);
	return states;
}

int compare_states_on_speed(unsigned long *a, unsigned long *b)
{
	if (a[SPEED_IDX] < b[SPEED_IDX]) return -1;
	if (a[SPEED_IDX] > b[SPEED_IDX]) return 1;
	if (a[POWER_IDX] < b[POWER_IDX]) return -1;
	if (a[POWER_IDX] > b[POWER_IDX]) return 1;
	return 0;
}

int main(int argc, char **argv)
{
	struct cpufreq_available_frequencies *freq_list;
	int core_count, freq_count;
	unsigned long *freq_array;
	int i, j;
	unsigned long *states;
	int state_count;

	core_count = get_core_count();
	freq_list = cpufreq_get_available_frequencies(0);
	freq_count = create_freq_array(freq_list, &freq_array);
	
	states = create_machine_states(core_count, freq_count, freq_array);
	qsort(states, state_count, STATE_SIZE(core_count), compare_states_on_speed);
	
	printf("speed\tpower");
	for (j = 0; j < core_count; j++)
		printf("\tcore%d", j);
	printf("\n");
	
	for (i = 0; i < state_count; i++) {
		printf("%u\t%u", state[SPEED_IDX], state[POWER_IDX]);
		for (j = 0; j < core_count; j++)
			printf("\t%u", state[CORE_IDX(j)]);
		printf("\n");
	}
	
	return 0;
}
