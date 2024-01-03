#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/delay.h>

struct kthread_data {
	char *name;
	struct semaphore *sem1;
	struct semaphore *sem2;
};

struct kthread_data prod, cons;
struct semaphore psem, csem;

struct task_struct *pthread, *cthread;

int prod_thread(void *data)
{
	struct kthread_data *pdata  =  (struct kthread_data *) data;

	while(1) {
		down(pdata->sem1);
		pr_info("%s:Gnerating data\n", pdata->name);
		mdelay(1000);
		pr_info("%s: Notify Consumer\n", pdata->name);
		up(pdata->sem2);
		if(kthread_should_stop())
			break;
	}
	return 0;
}

int cons_thread(void *data)
{
	struct kthread_data *pdata  =  (struct kthread_data *) data;

	while(1) {
		down(pdata->sem1);
		pr_info("%s:Signal recived from producer\n", pdata->name);
		mdelay(1000);
		pr_info("%s: Done Consuming Notify Producer and wait for next chunck\n", pdata->name);
		up(pdata->sem2);
		if(kthread_should_stop())
			break;
	}
	return 0;

}

int __init kthr_init(void)
{
	sema_init(&psem, 1);
	sema_init(&csem, 0);

	prod.name = "prod_thread";
	cons.name = "cons_thread";

	prod.sem1 = &psem;
	prod.sem2 = &csem;

	cons.sem1 = &csem;
	cons.sem2 = &psem;

	pthread = kthread_run(prod_thread, &prod, "prod_thread");
        if(IS_ERR(pthread)){
                pr_err("%s: unable to start kernel thread\n",__func__);
                return PTR_ERR(pthread);
        }

	cthread = kthread_run(cons_thread, &cons, "conse_thread");
        if(IS_ERR(cthread)){
                pr_err("%s: unable to start kernel thread\n",__func__);
                return PTR_ERR(cthread);
        }

	return 0;
}

void __exit kthr_exit(void)
{
	kthread_stop(pthread);
	kthread_stop(cthread);
}

module_init(kthr_init);
module_exit(kthr_exit);

MODULE_AUTHOR("Sidraya");
MODULE_DESCRIPTION("Produce and Consumer Programm");
MODULE_LICENSE("Dual MIT/GPL");
