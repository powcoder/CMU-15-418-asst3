https://powcoder.com
代写代考加微信 powcoder
Assignment Project Exam Help
Add WeChat powcoder
#include "crun.h"


/* Compute ideal load factor (ILF) for node */
static inline double neighbor_ilf(state_t *s, int nid) {
    graph_t *g = s->g;
    int outdegree = g->neighbor_start[nid+1] - g->neighbor_start[nid] - 1;
    int *start = &g->neighbor[g->neighbor_start[nid]+1];
    int i;
    double sum = 0.0;
    for (i = 0; i < outdegree; i++) {
	int lcount = s->rat_count[nid];
	int rcount = s->rat_count[start[i]];
	double r = imbalance(lcount, rcount);
	sum += r;
    }
    double ilf = BASE_ILF + 0.5 * (sum/outdegree);
    return ilf;
}

/* Compute weight for node nid */
static inline double compute_weight(state_t *s, int nid) {
    int count = s->rat_count[nid];
    double ilf = neighbor_ilf(s, nid);
    return mweight((double) count/s->load_factor, ilf);
}

/* Compute sum of weights in region of nid */
static inline double compute_sum_weight(state_t *s, int nid) {
    graph_t *g = s->g;
    double sum = 0.0;
    int eid;
    int eid_start = g->neighbor_start[nid];
    int eid_end  = g->neighbor_start[nid+1];
    int *neighbor = g->neighbor;
    for (eid = eid_start; eid < eid_end; eid++) {
	int nbrnid = neighbor[eid];
	double w = compute_weight(s, nbrnid);
	s->node_weight[nbrnid] = w;
	sum += w;
	
    }
    return sum;
}

#if DEBUG
/** USEFUL DEBUGGING CODE **/
static void show_weights(state_t *s) {
    int nid, eid;
    graph_t *g = s->g;
    int nnode = g->nnode;
    int *neighbor = g->neighbor;
    outmsg("Weights\n");
    for (nid = 0; nid < nnode; nid++) {
	int eid_start = g->neighbor_start[nid];
	int eid_end  = g->neighbor_start[nid+1];
	outmsg("%d: [sum = %.3f]", nid, compute_sum_weight(s, nid));
	for (eid = eid_start; eid < eid_end; eid++) {
	    outmsg(" %.3f", compute_weight(s, neighbor[eid]));
	}
	outmsg("\n");
    }
}
#endif

/* Recompute all node counts according to rat population */
static inline void take_census(state_t *s) {
    graph_t *g = s->g;
    int nnode = g->nnode;
    int *rat_position = s->rat_position;
    int *rat_count = s->rat_count;
    int nrat = s->nrat;

    memset(rat_count, 0, nnode * sizeof(int));
    int ri;
    for (ri = 0; ri < nrat; ri++) {
	rat_count[rat_position[ri]]++;
    }
}

/* Recompute all node weights */
static inline void compute_all_weights(state_t *s) {
    int nid;
    graph_t *g = s->g;
    double *node_weight = s->node_weight;

    for (nid = 0; nid < g->nnode; nid++)
	node_weight[nid] = compute_weight(s, nid);
}

/* In synchronous or batch mode, can precompute sums for each region */
static inline void find_all_sums(state_t *s) {
    graph_t *g = s->g;
    init_sum_weight(s);
    int nid, eid;
    for (nid = 0; nid < g->nnode; nid++) {
	double sum = 0.0;
	for (eid = g->neighbor_start[nid]; eid < g->neighbor_start[nid+1]; eid++) {
	    sum += s->node_weight[g->neighbor[eid]];
	    s->neighbor_accum_weight[eid] = sum;
	}
	s->sum_weight[nid] = sum;
    }
}

/*
  Given list of integer counts, generate real-valued weights
  and use these to flip random coin returning value between 0 and len-1
  This version only gets used in ratorder mode
*/
static inline int next_random_move(state_t *s, int r) {
    int nid = s->rat_position[r];
    int nnid = -1;
    random_t *seedp = &s->rat_seed[r];
    double tsum = compute_sum_weight(s, nid);
    graph_t *g = s->g;
    int eid;
    
    double val = next_random_float(seedp, tsum);

    double psum = 0.0;
    for (eid = g->neighbor_start[nid]; eid < g->neighbor_start[nid+1]; eid++) {
	/* Node weights valid, since were computed by compute_sum_weight or earlier */
	psum += s->node_weight[g->neighbor[eid]];
	if (val < psum) {
	    nnid = g->neighbor[eid];
	    break;
	}
    }

    if (nnid == -1) {
	/* Shouldn't get here */
	int degree = g->neighbor_start[nid+1] - g->neighbor_start[nid];
	outmsg("Internal error.  next_random_move.  Didn't find valid move.  Node %d. Degree = %d, Target = %.2f/%.2f.  Limit = %.2f\n",
	       nid, degree, val, tsum, psum);
	nnid = 0;
    }

    return nnid;
}

/*
  Given list of increasing numbers, and target number,
  find index of first one where target is less than list value
*/

/*
  Linear search
 */
static inline int locate_value_linear(double target, double *list, int len) {
    int i;
    for (i = 0; i < len; i++)
	if (target < list[i])
	    return i;
    /* Shouldn't get here */
    return -1;
}
/*
  Binary search down to threshold, and then linear
 */
static inline int locate_value(double target, double *list, int len) {
    int left = 0;
    int right = len-1;
    while (left < right) {
	if (right-left+1 < BINARY_THRESHOLD)
	    return left + locate_value_linear(target, list+left, right-left+1);
	int mid = left + (right-left)/2;
	if (target < list[mid])
	    right = mid;
	else
	    left = mid+1;
    }
    return right;
}


/*
  Version that can be used in synchronous or batch mode, where certain that node weights are already valid.
  And have already computed sum of weights for each node, and cumulative weight for each neighbor
  Given list of integer counts, generate real-valued weights
  and use these to flip random coin returning value between 0 and len-1
*/
static inline int fast_next_random_move(state_t *s, int r) {
    int nid = s->rat_position[r];
    graph_t *g = s->g;
    random_t *seedp = &s->rat_seed[r];
    /* Guaranteed that have computed sum of weights */
    double tsum = s->sum_weight[nid];    
    double val = next_random_float(seedp, tsum);

    int estart = g->neighbor_start[nid];
    int elen = g->neighbor_start[nid+1] - estart;
    int offset = locate_value(val, &s->neighbor_accum_weight[estart], elen);
#if DEBUG
    if (offset < 0) {
	/* Shouldn't get here */
	outmsg("Internal error.  fast_next_random_move.  Didn't find valid move.  Target = %.2f/%.2f.\n",
	       val, tsum);
	return 0;
    }
#endif
    return g->neighbor[estart + offset];
}

/* Step when in synchronous mode */
static void synchronous_step(state_t *s) {
    int ri;

    find_all_sums(s);
    for (ri = 0; ri < s->nrat; ri++) {
	s->rat_position[ri] = fast_next_random_move(s, ri);
    }
    take_census(s);
    compute_all_weights(s);
}

/* Process single batch */
static inline void do_batch(state_t *s, int bstart, int bcount) {
    int ni, ri;
    graph_t *g = s->g;
    int nnode = g->nnode;
    find_all_sums(s);
    clear_delta_count(s);
    for (ri = 0; ri < bcount; ri++) {
	int rid = ri+bstart;
	int onid = s->rat_position[rid];
	int nnid = fast_next_random_move(s, rid);
	s->rat_position[rid] = nnid;
	s->delta_rat_count[onid] -= 1;
	s->delta_rat_count[nnid] += 1;
    }

    /* Must first update all rat counts and then recompute weights */
    for (ni = 0; ni < nnode; ni++) {
	s->rat_count[ni] += s->delta_rat_count[ni];
    }

    /* Update weights */
    compute_all_weights(s);
}

static void batch_step(state_t *s) {
    int rid = 0;
    int bsize = s->batch_size;
    int nrat = s->nrat;
    int bcount;
    init_delta_count(s);
    while (rid < nrat) {
	bcount = nrat - rid;
	if (bcount > bsize)
	    bcount = bsize;
	do_batch(s, rid, bcount);
	rid += bcount;
    }
}

static void ratorder_step(state_t *s) {
    int rid;
    for (rid = 0; rid < s->nrat; rid++) {
	int onid = s->rat_position[rid];
	int nnid = next_random_move(s, rid);
	s->rat_position[rid] = nnid;
	s->rat_count[onid]--;
	s->rat_count[nnid]++;
	s->node_weight[onid] = compute_weight(s, onid);
	s->node_weight[nnid] = compute_weight(s, nnid);
    }
}

typedef void (*stepper_t)(state_t *s);

double simulate(state_t *s, int count, update_t update_mode, int dinterval, bool display) {
    int i;
    /* Compute and show initial state */
    bool show_counts = true;
    stepper_t step_function;
    if (update_mode == UPDATE_SYNCHRONOUS)
	step_function = synchronous_step;
    else if (update_mode == UPDATE_BATCH)
	step_function = batch_step;
    else
	step_function = ratorder_step;

    double start = currentSeconds();
    take_census(s);
    compute_all_weights(s);
    if (display)
	show(s, show_counts);

    for (i = 0; i < count; i++) {
	step_function(s);
	if (display) {
	    show_counts = (((i+1) % dinterval) == 0) || (i == count-1);
	    show(s, show_counts);
	}
    }
    double delta = currentSeconds() - start;
    done();
    return delta;
}
