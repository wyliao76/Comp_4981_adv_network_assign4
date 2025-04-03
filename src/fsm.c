#include "fsm.h"
#include <stdio.h>

fsm_state_func fsm_transition(fsm_state_t from_id, fsm_state_t to_id, const struct fsm_transition transitions[])
{
    fsm_state_func transition_func;

    transition_func = NULL;

    for(size_t i = 0; transitions[i].perform != NULL; i++)
    {
        if(transitions[i].from_id == from_id && transitions[i].to_id == to_id)
        {
            transition_func = transitions[i].perform;
            break;
        }
    }

    return transition_func;
}
