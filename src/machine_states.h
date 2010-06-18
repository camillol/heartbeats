/*
 *  machine_states.h
 *  heartbeats
 *
 *  Created by Camillo Lugaresi on 17/06/10.
 *
 */

#define SPEED_IDX 0
#define POWER_IDX 1
#define CORE_IDX(core) ((core) + 2)
#define STATE_LEN(core_count) (CORE_IDX(core_count))
#define STATE_SIZE(core_count) (sizeof(unsigned long) * STATE_LEN(core_count))
#define STATE_I(states, core_count, i) ((states) + STATE_LEN(core_count) * (i))

void calculate_state_properties(unsigned long *state, int core_count);
unsigned long *create_machine_states(int *state_count, int core_count, int freq_count, unsigned long *freq_array);
int compare_states_on_speed(const void *a, const void *b);
int redundant_state(unsigned long *state, int core_count);
int pareto_optimal(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count);
int drop_equivalent(unsigned long *state, int state_index, unsigned long *states, int state_count, int core_count);
