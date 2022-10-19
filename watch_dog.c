/*****************************************************   
 * Author : Avia Avikasis                            *
 * Reviewer: Ester Shpoker                           *
 * 10/09/2022                                        *
 * Description : watch dog implementation            *
 *                                                   *
 *****************************************************/
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500

#include <pthread.h>   /* pthread_create */
#include <semaphore.h> /* sem_init, sem_wait, sem_post */
#include <assert.h>    /* assert */
#include <signal.h>    /* sigaction, SIGUSR1, SIGUSR2 */
#include <stdlib.h>    /* getenv, setenv, unsetenv, realpath */
#include <limits.h>    /* PATH_MAX */
#include <stdio.h>     /* printf */
#include <stdatomic.h> /* atomic size_t */
#include <fcntl.h>     /* O_CREATE */
#include <sys/types.h> /* pid_t */
#include <unistd.h>    /* getpid */

#include "watch_dog.h" 
#include "scheduler.h" /* scheduler api */
#include "uid.h"       /* uid api */
#include "utils.h"     /* util api */

#define TRUE 1
#define EXEC_PARAMS 1000
#define NUM_STR_SIZE 20
#define UNUSED(x) (void)x
#define FAIL_RET -1


typedef struct task_params
{
    int pid;
    size_t threshold;
    scheduler_ty *scheduler;
}task_params_ty; 

typedef struct mmi_params
{
    char **client_argv;
    size_t threshold;
    size_t interval;
}mmi_params_ty;

typedef enum
{
    CLIENT_PATH = 0,
    CLIENT_PID  = 1,
    THRESHOLD   = 2,
    INTERVAL    = 3
}exec_params;

mmi_params_ty thread_params = {0};
static atomic_size_t g_counter = 0;
static sem_t *client_sem;
pthread_t wd_communicate;
static sem_t *watch_process_sem;
scheduler_ty *scheduler = NULL;

static void Sigusr1Handler(int sig, siginfo_t *info, void *ucontext);
static void Sigusr2Handler(int sig, siginfo_t *info, void *ucontext);
static void *CommunicateHandler(void *sem);
static int SendSignal(void *params);
static status_ty InitExecParams(mmi_params_ty *thread_params,
                                         char *exec_params[], 
                                        char *path_buffer, 
                                        char *ppid_str, 
                                        char *threshold_str, 
                                        char *interval_str);

static status_ty InitScheduler(scheduler_ty *scheduler,
                            task_params_ty *task_params,
                             mmi_params_ty *thread_params);
static int StopScheduler(void *params);


static void CleanScheduler(void *scheduler)
{
    assert(NULL != scheduler);

	SchedulerClear(scheduler);
}

static int SendSignal(void *params)
{
    task_params_ty *task_params = params;

    assert(NULL != task_params);

   	kill(task_params->pid, SIGUSR1); 
    atomic_fetch_add(&g_counter, 1);

    #ifndef NDEBUG
    printf("\nclient : i sent sigusr1\n");
    #endif

    return 0;
}

static int StopScheduler(void *params)
{
    task_params_ty *task_params = params;

    assert(NULL != task_params);

    if (task_params->threshold < g_counter)
    {
        SchedulerStop(task_params->scheduler);
        atomic_fetch_sub(&g_counter, g_counter);

        #ifndef NDEBUG
        printf("\nclient : stop scheduler\n");
        #endif

        return 1;
    }
    return 0;
}

static void Sigusr1Handler(int sig, siginfo_t *info, void *ucontext)
{
    UNUSED(sig);
    UNUSED(info);
    UNUSED(ucontext);

    #ifndef NDEBUG
    printf("\nclient : i got sigusr1\n");
    #endif

    atomic_fetch_sub(&g_counter, g_counter);
}

static void Sigusr2Handler(int sig, siginfo_t *info, void *ucontext)
{
    UNUSED(sig);
    UNUSED(info);
    UNUSED(ucontext);

    /* the watch dog get some error */
    #ifndef NDEBUG
    printf("\nclient : i got sigusr2\n");
    #endif
    /* kill the watch dog process */
    kill(atoi(getenv("WD_PID")), SIGUSR2); 
}

static status_ty InitExecParams(mmi_params_ty *thread_params,
                                         char *exec_params[], 
                                        char *path_buffer, 
                                        char *ppid_str, 
                                        char *threshold_str,
                                        char *interval_str)
{
    size_t i = 1;

    assert(NULL != thread_params);
    assert(NULL != exec_params);
    assert(NULL != path_buffer);
    assert(NULL != ppid_str);
    assert(NULL != threshold_str);
    assert(NULL != interval_str);

    realpath(thread_params->client_argv[0], path_buffer);
    if (NULL == path_buffer)
    {
        return FAIL;
    }
    exec_params[CLIENT_PATH] = path_buffer;
    if (0 > sprintf(ppid_str, "%d", getppid()))
    {
        return FAIL;
    }
    exec_params[CLIENT_PID] = ppid_str;
    if (0 > sprintf(threshold_str, "%ld", thread_params->threshold))
    {
        return FAIL;
    }
    exec_params[THRESHOLD] = threshold_str;
    if (0 > sprintf(interval_str, "%ld", thread_params->interval))
    {
        return FAIL;
    }
    exec_params[INTERVAL] = interval_str;

    for ( ; NULL != thread_params->client_argv[i] ; ++i)
    {
        exec_params[i + 3] = thread_params->client_argv[i]; 
    }

    return SUCCESS;
}


static status_ty InitScheduler(scheduler_ty *scheduler,
                            task_params_ty *task_params,
                             mmi_params_ty *thread_params)
{
    uid_ty uid = {0};
    
    uid = SchedulerAddTask(scheduler, &SendSignal, task_params,
                             NULL, NULL, thread_params->interval);
    if (UidIsEqual(bad_uid, uid))
    {
        SchedulerDestroy(scheduler);
        return FAIL;
    }

    uid = SchedulerAddTask(scheduler, &StopScheduler,
                 task_params, &CleanScheduler, scheduler, 1);
    if (UidIsEqual(bad_uid, uid))
    {
        SchedulerDestroy(scheduler);
        return FAIL;
    }

    return SUCCESS;     
}

static status_ty CreateWatchDog(mmi_params_ty *thread_params)
{
    int pid = 0;
    int exec_ret = 0;

    char *exec_params[EXEC_PARAMS] = {0};
    char path_buffer[PATH_MAX] = {'\0'};
    char pid_str[NUM_STR_SIZE] = {'\0'};
    char ppid_str[NUM_STR_SIZE] = {'\0'};
    char threshold_str[NUM_STR_SIZE] = {'\0'};
    char interval_str[NUM_STR_SIZE] = {'\0'};
    
    #ifndef NDEBUG
    printf("\nclient: i create new watchdog\n");
    #endif

    pid = fork();
    if (FAIL_RET == pid)
    {
        return FAIL;
    }
    if (0 == pid)   /* it is child */
    {
        /* create the wd process */
        InitExecParams(thread_params, exec_params,
                            path_buffer, ppid_str, threshold_str, interval_str);

        exec_ret = execv("./watch_process.out", exec_params);
        if (FAIL_RET == exec_ret)
        {
            return FAIL;
        }
    }
    else  /* it is parent */
    {
        sprintf(pid_str, "%d", pid);
        if (FAIL_RET == setenv("WD_PID", pid_str, TRUE))
        {
            return FAIL;
        }
    } 

    return SUCCESS;   
}

static void InitTaskParams(task_params_ty *task_params,
                         mmi_params_ty *thread_params,
                          scheduler_ty *scheduler)
{
    task_params->pid = atoi(getenv("WD_PID"));
    task_params->threshold = thread_params->threshold;
    task_params->scheduler = scheduler;    
}

static void InitSigusr1(struct sigaction *sigusr1)
{
    assert(NULL != sigusr1);

    sigusr1->sa_flags = SA_SIGINFO;
    sigusr1->sa_sigaction = &Sigusr1Handler;
}

static void InitSigusr2(struct sigaction *sigusr2)
{
    assert(NULL != sigusr2);

    sigusr2->sa_flags = SA_SIGINFO;
    sigusr2->sa_sigaction = &Sigusr2Handler;
}

static void *CommunicateHandler(void *thread_param)
{
    struct sigaction sigusr1 = {NULL};
    struct sigaction sigusr2 = {NULL};
    task_params_ty task_params = {0};
    mmi_params_ty *thread_params = thread_param;

    scheduler = SchedulerCreate();
    if (NULL == scheduler)
    {
        return NULL;
    }

    while (TRUE)
    {
       
        if (NULL == getenv("WD_PID"))  /* watch dog isn't already exist */
        {
            if (FAIL == CreateWatchDog(thread_params))
            {
                return NULL;
            }
        }

        InitTaskParams(&task_params, thread_params, scheduler);

        InitSigusr1(&sigusr1);
        InitSigusr2(&sigusr2);
     
        if (FAIL_RET == sigaction(SIGUSR1, &sigusr1, NULL))
        {
            return NULL;
        }
        if (FAIL_RET == sem_post(client_sem))
        {
            return NULL;
        }
        if (FAIL == InitScheduler(scheduler, &task_params, thread_params))
        {
            return NULL;
        }
        if (FAIL_RET == sem_wait(watch_process_sem))
        {
            return NULL;
        }

        if (FAIL == SchedulerRun(scheduler))
        {
            return NULL;
        }

        /* the counter increased over the max and the scheduler stoped */
        if (FAIL_RET == unsetenv("WD_PID"))
        {
            return NULL;
        }
    }
}

static wd_status OpenSem(void)
{
    client_sem = sem_open("/client_sem", O_CREAT, 0777, 0);
    if (SEM_FAILED == client_sem)
    {
        return WD_FAIL;
    }
    watch_process_sem = sem_open("/watch_process_sem", O_CREAT, 0777, 0);
    if (SEM_FAILED == watch_process_sem)    
    {
        return WD_FAIL;
    }
}

wd_status MakeMeImortal(char *client_argv[], size_t threshold, size_t interval)
{
    int create_ret = 0;
   
    assert(NULL != client_argv);

    if (WD_FAIL == OpenSem())
    {
        return WD_FAIL;
    }

    thread_params.client_argv = client_argv;
    thread_params.threshold = threshold;
    thread_params.interval = interval;

    /* create the thread of communication with the wd process */
    create_ret = pthread_create(&wd_communicate, NULL, 
                    &CommunicateHandler, &thread_params);
    pthread_detach(wd_communicate);
    if (FAIL_RET == create_ret)
    {
        return WD_FAIL;
    }

    return WD_READY;
}

void LetMeDie()
{  
    kill(atoi(getenv("WD_PID")), SIGUSR2); 
    SchedulerStop(scheduler);
    SchedulerClear(scheduler);
    SchedulerDestroy(scheduler);
    sem_destroy(client_sem);
    sem_destroy(watch_process_sem);
}



















