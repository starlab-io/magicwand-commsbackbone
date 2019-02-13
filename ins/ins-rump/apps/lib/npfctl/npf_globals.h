#ifndef _NPF_GLOBALS_H_
#define _NPF_GLOBALS_H_

#define MAX_RULE_NESTING 16

void npfctl_fini( void );

struct npf_extmod {
	char *			name;
	npfext_initfunc_t	init;
	npfext_consfunc_t	cons;
	npfext_paramfunc_t	param;
	struct npf_extmod *	next;
};

typedef struct {
	nl_config_t *	conf;
	FILE *		fp;
	long		fpos;
	u_int		flags;
	uint32_t	curmark;
} npf_conf_info_t;


typedef struct npf_element {
	void *		e_data;
	int		e_type;
	struct npf_element *e_next;
} npf_element_t;

typedef struct npfvar {
	char *		v_key;
	npf_element_t *	v_elements;
	npf_element_t *	v_last;
	int		v_type;
	size_t		v_count;
	void *		v_next;
} npfvar_t;

typedef struct _g_npfvals_t
{
    //npf_build.c globals
    nl_config_t *    npf_conf;
    bool             npf_debug;
    nl_rule_t *      the_rule;

    nl_rule_t *      current_group[MAX_RULE_NESTING];
    unsigned         rule_nesting_level;
    nl_rule_t *      defgroup;

    //npf_data.c
    struct ifaddrs * ifs_list;

    //npf_extmod
    struct npf_extmod * npf_extmod_list;

    //npf_show.c
    npf_conf_info_t stdout_ctx;

    //npf_var.c
    npfvar_t * var_list;
    size_t     var_num;
    
} g_npfvals_t;

g_npfvals_t g_npfvals;

#endif /*_NPF_GLOBALS_H_*/
