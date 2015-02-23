#include <linux/sched.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/limits.h>
#include <media/rc-core.h>

#include "sched.h"

#define BALANCE MS_TO_NS(500)

struct grr_timer_balancer period_timer;

static inline
void inc_grr_tasks(struct sched_grr_entity *grr_se, struct grr_rq *grr_rq)
{
	grr_rq->grr_nr_running++;
}

static inline struct rq *rq_of_grr_rq(struct grr_rq *grr_rq)
{
	return container_of(grr_rq, struct rq, grr);
}

static inline struct task_struct *grr_task_of(struct sched_grr_entity *grr_se)
{
	return container_of(grr_se, struct task_struct, grr);
}

static inline struct grr_rq *grr_rq_of_se(struct sched_grr_entity *grr_se)
{
	struct task_struct *p = grr_task_of(grr_se);
	struct rq *rq = task_rq(p);

	return &rq->grr;
}

static inline int on_grr_rq(struct sched_grr_entity *grr_se)
{
	return !list_empty(&grr_se->run_list);
}

static void __enqueue_grr_entity(struct sched_grr_entity *grr_se, bool head)
{
	struct grr_rq *grr_rq = grr_rq_of_se(grr_se);
	struct list_head *queue = &grr_rq->head.run_list;
	struct task_struct *d_task;
	
	if (head)
		list_add(&grr_se->run_list, queue);
	else
		list_add_tail(&grr_se->run_list, queue);

	d_task = container_of(list_first_entry(queue,
					       struct sched_grr_entity,
					       run_list),
			      struct task_struct, grr);

	inc_grr_tasks(grr_se, grr_rq);
}

static void enqueue_grr_entity(struct sched_grr_entity *grr_se, bool head)
{
	__enqueue_grr_entity(grr_se, head);
}

static void update_curr_grr(struct rq *rq)
{
	u64 delta_exec;
	struct task_struct *curr_grr = rq->curr;
	
	delta_exec = rq->clock_task - curr_grr->se.exec_start;
	curr_grr->se.sum_exec_runtime += delta_exec;
	curr_grr->se.exec_start = rq->clock_task;
}

static void
enqueue_task_grr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_grr_entity *grr_se = &p->grr;

	if (flags & ENQUEUE_WAKEUP)
		grr_se->timeout = 0;

	enqueue_grr_entity(grr_se, flags & ENQUEUE_HEAD);
	inc_nr_running(rq);
}


static inline
void dec_grr_tasks(struct sched_grr_entity *grr_se, struct grr_rq *grr_rq)
{
	grr_rq->grr_nr_running--;
}

static void dequeue_grr_entity(struct sched_grr_entity *grr_se)
{
	struct grr_rq *grr_rq = grr_rq_of_se(grr_se);

	list_del_init(&grr_se->run_list);
	dec_grr_tasks(grr_se, grr_rq);
}

static void
dequeue_task_grr(struct rq *rq, struct task_struct *p, int flags)
{
	dequeue_grr_entity(&p->grr);
	update_curr_grr(rq);
	dec_nr_running(rq);
}

static bool
yield_to_task_grr(struct rq *rq, struct task_struct *p, bool preempty)
{
	return true;
}

static void
check_preempt_curr_grr(struct rq *rq, struct task_struct *p, int flags)
{
	resched_task(rq->curr);
}

struct task_struct *pick_next_task_grr(struct rq *rq)
{
	struct list_head *head = &rq->grr.head.run_list;
	struct sched_grr_entity *grr_se;
	struct task_struct *iter;

	if (list_empty(head))
		return NULL;
	grr_se = list_first_entry(head, struct sched_grr_entity, run_list);
	iter = grr_task_of(grr_se);
/* 	printk("pnt %s\n", iter->comm);*/ 
	iter->se.exec_start = rq->clock_task;
	return iter;
}

void
put_prev_task_grr(struct rq *rq, struct task_struct *p)
{
	update_curr_grr(rq);
}

void
set_curr_task_grr(struct rq *rq)
{
	update_curr_grr(rq);
}

static void watchdog(struct rq *rq, struct task_struct *p)
{
	unsigned long soft, hard;

	/* max may change after cur was read, this will be fixed next tick */
	soft = task_rlimit(p, RLIMIT_RTTIME);
	hard = task_rlimit_max(p, RLIMIT_RTTIME);

	if (soft != RLIM_INFINITY) {
		unsigned long next;

		p->grr.timeout++;
		next = DIV_ROUND_UP(min(soft, hard), USEC_PER_SEC/HZ);
		if (p->grr.timeout > next)
			p->cputime_expires.sched_exp = p->se.sum_exec_runtime;
	}
}


/*
 * Put task to the head or the end of the run list without the overhead of
 * dequeue followed by enqueue.
 */
static void
requeue_grr_entity(struct grr_rq *grr_rq, struct sched_grr_entity *grr_se, int head)
{
	if (on_grr_rq(grr_se)) {
		struct list_head *queue = &grr_rq->head.run_list;

		if (head)
			list_move(&grr_se->run_list, queue);
		else
			list_move_tail(&grr_se->run_list, queue);
	}
}

static void requeue_task_grr(struct rq *rq, struct task_struct *p, int head)
{
	struct sched_grr_entity *grr_se = &p->grr;
	struct grr_rq *grr_rq;

	grr_rq = grr_rq_of_se(grr_se);
	requeue_grr_entity(grr_rq, grr_se, head);
}

static void yield_task_grr(struct rq *rq)
{
	requeue_task_grr(rq, rq->curr, 0);
}


#ifdef CONFIG_SMP
static int task_group_state(struct task_struct *task)
{
	char group_path[30];

	cgroup_path(task_group(task)->css.cgroup, group_path, 30);
	if (!strcmp(group_path, "/") || !strcmp(group_path, "/apps"))
		return FOREGROUND;
	else if (!strcmp(group_path, "/apps/bg_non_interactive"))
		return BACKGROUND;
	return -1;
}

static int find_highest_rq(struct task_struct *task, int curr_cpu)
{
	int iter_cpu, start_cpu = 0;
	int highest, highest_cpu;
	int end_cpu = NR_CPUS;

	highest_cpu = start_cpu;
	highest = cpu_rq(start_cpu)->nr_running;

	if (task) {
		switch (task_group_state(task)) {
		case FOREGROUND:
			start_cpu = 0;
			end_cpu = nr_cpus_foreground;
			highest_cpu = start_cpu;
			highest = cpu_rq(start_cpu)->nr_running;
			break;
		case BACKGROUND:
			start_cpu = nr_cpus_foreground;
			end_cpu = NR_CPUS;
			highest_cpu = start_cpu;
			highest = cpu_rq(start_cpu)->nr_running;
			break;
		default:
			BUG_ON(task);
			break;
		}
	} else {
		if (curr_cpu < 0)
			curr_cpu = task_cpu(task);
		if (curr_cpu < nr_cpus_foreground) {
			start_cpu = 0;
			end_cpu = nr_cpus_foreground;
			highest_cpu = start_cpu;
			highest = cpu_rq(start_cpu)->nr_running;
		} else {
			start_cpu = nr_cpus_foreground;
			end_cpu = NR_CPUS;
			highest_cpu = start_cpu;
			highest = cpu_rq(start_cpu)->nr_running;
		}
	}

	for (iter_cpu = start_cpu; iter_cpu < end_cpu; iter_cpu++) {
		struct rq *rq = cpu_rq(iter_cpu);
		if (highest < rq->nr_running) {
			highest = rq->nr_running;
			highest_cpu = iter_cpu;
		}
	}
	return highest_cpu;
}

static int find_lowest_rq(struct task_struct *task, int curr_cpu)
{
	int iter_cpu, start_cpu = 0;
	int lowest, lowest_cpu;
	int end_cpu = NR_CPUS;

	lowest_cpu = start_cpu;
	lowest = cpu_rq(start_cpu)->nr_running;

	if (task) {
		switch (task_group_state(task)) {
		case FOREGROUND:
			start_cpu = 0;
			end_cpu = nr_cpus_foreground;
			lowest_cpu = start_cpu;
			lowest = cpu_rq(start_cpu)->nr_running;
			break;
		case BACKGROUND:
			start_cpu = nr_cpus_foreground;
			end_cpu = NR_CPUS;
			lowest_cpu = start_cpu;
			lowest = cpu_rq(start_cpu)->nr_running;
			break;
		default:
			BUG_ON(task);
			break;
		}
	} else {
		if (curr_cpu < 0)
			curr_cpu = task_cpu(task);
		if (curr_cpu < nr_cpus_foreground) {
			start_cpu = 0;
			end_cpu = nr_cpus_foreground;
			lowest_cpu = start_cpu;
			lowest = cpu_rq(start_cpu)->nr_running;
		} else {
			start_cpu = nr_cpus_foreground;
			end_cpu = NR_CPUS;
			lowest_cpu = start_cpu;
			lowest = cpu_rq(start_cpu)->nr_running;
		}
	}

	for (iter_cpu = start_cpu; iter_cpu < end_cpu; iter_cpu++) {
		struct rq *rq = cpu_rq(iter_cpu);
		if (lowest > rq->nr_running) {
			lowest = rq->nr_running;
			lowest_cpu = iter_cpu;
		}
	}
	return lowest_cpu;
}

static int
select_task_rq_grr(struct task_struct *p, int sd_flag, int flags)
{
	struct task_struct *curr;
	struct rq *rq;
	int cpu;

	cpu = task_cpu(p);

	if (p->rt.nr_cpus_allowed == 1)
		goto out;

	/* For anything but wake ups, just return the task_cpu */
	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK)
		goto out;

	rq = cpu_rq(cpu);

	rcu_read_lock();
	curr = ACCESS_ONCE(rq->curr); /* unlocked access */

	if (curr && p->grr.nr_cpus_allowed > 1) {
		int target = find_lowest_rq(p, task_cpu(p));
		if (target != -1)
			cpu = target;
	}
	rcu_read_unlock();

out:
	return cpu;
}

static
int can_migrate_task(struct task_struct *p, int src_cpu, int dst_cpu)
{
	if (!cpumask_test_cpu(dst_cpu, tsk_cpus_allowed(p))) {
		return 0;
	}

	if (task_running(cpu_rq(src_cpu), p)) {
		return 0;
	}

	/* Cache hot */
	return 1;
}

static void move_task(int src_cpu, int dst_cpu)
{
	struct list_head *head;
	struct sched_grr_entity *first_se, *grr_se;
	struct task_struct *p;
	struct rq *src_rq, *dst_rq;

	src_rq = cpu_rq(src_cpu);
	head = &src_rq->grr.head.run_list;
	if (list_empty(head))
		return;
	first_se = list_first_entry(head, struct sched_grr_entity, run_list);
	if (list_is_last(&first_se->run_list, head))
		return;
	grr_se = list_first_entry(&first_se->run_list,
				  struct sched_grr_entity, run_list);
	p = grr_task_of(grr_se);
	if (!can_migrate_task(p, src_cpu, dst_cpu))
		return;
	dst_rq = cpu_rq(dst_cpu);
	deactivate_task(src_rq, p, 0);
	activate_task(dst_rq, p, 0);
	/* if (task_notify_on_migrate(p)) */
	/*   per_cpu(dbs_boost_needed, dst_cpu) = true; */
}

static void load_balance(void)
{
	int highest_cpu, lowest_cpu;
	struct rq *hrq, *lrq;
	unsigned long flags;

	raw_spin_lock(&this_rq()->lock);
	highest_cpu = find_highest_rq(NULL, this_rq()->cpu);
	lowest_cpu = find_lowest_rq(NULL, this_rq()->cpu);
	hrq = cpu_rq(highest_cpu);
	lrq = cpu_rq(lowest_cpu);
	raw_spin_unlock(&this_rq()->lock);
	trace_printk("Highest cpu: %d\n", highest_cpu);
	trace_printk("***Highest had %lu jobs before\n", hrq->nr_running);
	if (hrq->nr_running - lrq->nr_running > 1) {
		local_irq_save(flags);
		double_rq_lock(hrq, lrq);
		move_task(highest_cpu, lowest_cpu);
		double_rq_unlock(hrq, lrq);
		local_irq_restore(flags);
	}
	trace_printk("   Highest had %lu jobs after***\n",
		     cpu_rq(highest_cpu)->nr_running);
}

static enum hrtimer_restart test_timer(struct hrtimer *timer)
{
	ktime_t now;
	int overrun;

	for (;;) {
		now = hrtimer_cb_get_time(timer);
		overrun = hrtimer_forward(timer, now,
					  ns_to_ktime(BALANCE));
		if (!overrun)
			break;
		load_balance();
	}
	return HRTIMER_RESTART;
}

void init_sched_grr_class(void)
{
#ifdef CONFIG_SMP
	hrtimer_init(&period_timer.grr_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_ABS);
	period_timer.grr_timer.function = test_timer;
	hrtimer_start(&period_timer.grr_timer,
		      period_timer.grr_period,
		      HRTIMER_MODE_ABS);

#endif
}

static void pre_schedule_grr(struct rq *this_rq, struct task_struct *task)
{
}

static void post_schedule_grr(struct rq *this_rq)
{
}

static void task_woken_grr(struct rq *this_rq, struct task_struct *task)
{
	int new_cpu;

	rcu_read_lock();
	new_cpu = find_lowest_rq(task, this_rq->cpu);
	rcu_read_unlock();

	if (new_cpu != task_cpu(task))
		resched_task(task);
}

static void
set_cpus_allowed_grr(struct task_struct *p, const struct cpumask *newmask)
{
}

#endif

void task_tick_grr(struct rq *rq, struct task_struct *p, int queued)
{
	struct sched_grr_entity *grr_se = &p->grr;
	struct sched_grr_entity *iter;
	struct task_struct *p_iter;

	update_curr_grr(rq);

	watchdog(rq, p);

	if (--p->grr.time_slice)
		return;

	p->grr.time_slice = RR_TIMESLICE;
	list_for_each_entry(iter, &rq->grr.head.run_list, run_list) {
		p_iter = container_of(iter, struct task_struct, grr);
/*		printk("item is: %s; pid: %d; policy: %d\n", */
		/* p_iter->comm, p_iter->pid, p_iter->policy); */
	}

	if (grr_se->run_list.prev != grr_se->run_list.next) {
		requeue_task_grr(rq, p, 0);
		set_tsk_need_resched(p);
		return;
	}
}

void switched_from_grr(struct rq *this_rq, struct task_struct *task)
{
	/* printk("switched from grr called\n"); */
}

void
switched_to_grr(struct rq *this_rq, struct task_struct *task)
{
	/* printk("Switched to grr, process was: %s and cpu runqueue had: %s\n", */
	/*        task->comm, */
	/*        this_rq->curr->comm); */
}

unsigned int get_rr_interval_grr(struct rq *rq, struct task_struct *task)
{
	return RR_TIMESLICE;
}

void init_grr_rq(struct grr_rq *grr_rq)
{
	struct list_head *run_list_head = &grr_rq->head.run_list;
	INIT_LIST_HEAD(run_list_head);
}

const struct sched_class grr_sched_class = {
	.next			= &fair_sched_class,
	.enqueue_task		= enqueue_task_grr,
	.dequeue_task		= dequeue_task_grr,
	.yield_task             = yield_task_grr,
	.yield_to_task          = yield_to_task_grr,
	.check_preempt_curr	= check_preempt_curr_grr,
	.pick_next_task		= pick_next_task_grr,
	.put_prev_task		= put_prev_task_grr,
	
#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_grr,
	.pre_schedule           = pre_schedule_grr,
	.post_schedule          = post_schedule_grr,
	.task_woken             = task_woken_grr,
	.set_cpus_allowed       = set_cpus_allowed_grr,
#endif

	.set_curr_task          = set_curr_task_grr,
	.task_tick		= task_tick_grr,

	.switched_from		= switched_from_grr,
	.switched_to		= switched_to_grr,

	.get_rr_interval	= get_rr_interval_grr,

/* #ifdef CONFIG_GRR_GROUP_SCHED */
/* 	.task_move_group	= task_move_group_grr, */
/* #endif */
};
