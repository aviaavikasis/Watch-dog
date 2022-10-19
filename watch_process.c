#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500

#include <pthread.h>   /* pthread_create */
#include <semaphore.h> /* sem_init, sem_wait, sem_post */
#include <assert.h>    /* assert */
#include <stdio.h>     /* printf */
#include <stdlib.h>    /* exit */
#include <signal.h>    /* sigaction */
#include <stdatomic.h> /* atomic size_t */

#include "scheduler.h" /* scheduler api */
#include "uid.h"       /* uid api */
#include "utils.h"     /* utils api */

#define TRUE 1
#define NUM_STR_SIZE 20
#define UNUSED(x) (void)x
#define FAIL_RET -1


typedef struct task_params
{
    int pid;
    size_t threshold;
}task_params_ty; 

typedef enum
{
    CLIENT_PATH = 0,
    CLIENT_PID  = 1,
    THRESHOLD   = 2,
    INTERVAL    = 3
}exec_params;

static atomic_size_t g_counter = 0;
static scheduler_ty *scheduler = NULL;
sem_t *client_sem;
sem_t *watch_process_sem;


static int SendSignal(void *params);
static void CleanScheduler(void *scheduler);
static int StopScheduler(void *params);
static void Sigusr1Handler(int sig, siginfo_t *info, void *ucontext);
static void Sigusr2Handler(int sig, siginfo_t *info, void *ucontext);
static void InitTaskParams(task_params_ty *task_params, char **argv);
static status_ty InitScheduler(task_params_ty *task_params, char *interval);
static status_ty ReviveClient(char **client_argv, task_params_ty *task_params);
static void ErrorHandling(task_params_ty *task_params);


static int SendSignal(void *params)
{
    task_params_ty *task_params = params;

    assert(NULL != task_params);

   	kill(task_params->pid, SIGUSR1); 
    atomic_fetch_add(&g_counter, 1);
    #ifndef NDEBUG
    printf("\nwatch_process : i sent sigusr1\n");
    #endif

    return 0;
}

static void CleanScheduler(void *scheduler)
{
    assert(NULL != scheduler);

	SchedulerClear(scheduler);
}

static int StopScheduler(void *params)
{
    task_params_ty *task_params = params;

    assert(NULL != task_params);

    if (task_params->threshold < g_counter)
    {
        SchedulerStop(scheduler);
        atomic_fetch_sub(&g_counter, g_counter);
        #ifndef NDEBUG
        printf("\nwatch_process : stop scheduler\n");
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
    printf("\nwatch_process : i got sigusr1\n");
    #endif
    
    atomic_fetch_sub(&g_counter, g_counter);
}

static void Sigusr2Handler(int sig, siginfo_t *info, void *ucontext)
{ 
    UNUSED(sig);
    UNUSED(info);
    UNUSED(ucontext);

    SchedulerStop(scheduler);
    SchedulerClear(scheduler);
    SchedulerDestroy(scheduler);

    exit(0);
}

static void InitTaskParams(task_params_ty *task_params, char **argv)
{
    task_params->pid = (0 == task_params->pid) ? atoi(argv[1]) : task_params->pid;
    task_params->threshold = atoi(argv[2]);
}

static status_ty InitScheduler(task_params_ty *task_params, char *interval)
{
    uid_ty uid = {0};
    
    assert(NULL != task_params);
    assert(NULL != interval);

    uid = SchedulerAddTask(scheduler, &SendSignal,
                    task_params, NULL, NULL, atoi(interval));
    if (UidIsEqual(bad_uid, uid))
    {
        SchedulerDestroy(scheduler);
        return FAIL;
    }

    uid = SchedulerAddTask(scheduler, &StopScheduler, 
            task_params, &CleanScheduler, scheduler, 2);
    if (UidIsEqual(bad_uid, uid))
    {
        SchedulerDestroy(scheduler);
        return FAIL;
    }  

    return SUCCESS;  
}

static status_ty ReviveClient(char **client_argv, task_params_ty *task_params)
{
    int pid = 0;

    assert(NULL != client_argv);
    assert(NULL != task_params);

    pid = fork();
    if (FAIL_RET == pid)
    {
        return FAIL;
    }
    if (0 == pid)    /* it is child */
    {
        execv(client_argv[CLIENT_PATH], client_argv + 3);
    }
    else
    {
        task_params->pid = pid;
    }

    return SUCCESS;
}

static void ErrorHandling(task_params_ty *task_params)
{
    assert(NULL != task_params);

    SchedulerDestroy(scheduler);
   	kill(task_params->pid, SIGUSR2); 
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


int main(int argc, char *argv[])
{
    struct sigaction sigusr1 = {0};
    struct sigaction sigusr2 = {0};
    
    task_params_ty task_params = {0};
    char pid_str[NUM_STR_SIZE] = {'\0'};
    scheduler = SchedulerCreate();
    UNUSED(argc);
    
    #ifndef NDEBUG
    printf("\ni am watch_process\n");
    printf("\nwatch_process pid : %d\n", getpid());
    #endif

    client_sem = sem_open("/client_sem", 0);
    if (SEM_FAILED == client_sem)
    {
        ErrorHandling(&task_params);
    }
    watch_process_sem = sem_open("/watch_process_sem", 0);
    if (SEM_FAILED == client_sem)
    {
        ErrorHandling(&task_params);
    }

    while(TRUE)
    {
        InitTaskParams(&task_params, argv);
        
        InitSigusr1(&sigusr1);
        InitSigusr2(&sigusr2);
        
        if (FAIL_RET == sigaction(SIGUSR1, &sigusr1, NULL))
        {
            ErrorHandling(&task_params);
        }
        if (FAIL_RET == sigaction(SIGUSR2, &sigusr2, NULL))
        {
            ErrorHandling(&task_params);
        }
        if (FAIL_RET == sem_post(watch_process_sem))
        {
            ErrorHandling(&task_params);
        }
        if (FAIL == InitScheduler(&task_params, argv[INTERVAL]))
        {
            ErrorHandling(&task_params);
        }
  
        if (FAIL_RET == sem_wait(client_sem))
        {
            ErrorHandling(&task_params);
        }

        if (FAIL == SchedulerRun(scheduler))
        {
            ErrorHandling(&task_params);
        }

        /* the counter increased over the max and the scheduler stoped */

        if (0 > sprintf(pid_str, "%d", getpid()))
        {
            ErrorHandling(&task_params);
        } 
        if (FAIL_RET == setenv("WD_PID", pid_str, TRUE))
        {
            ErrorHandling(&task_params);
        }

        /* create new client process */
        ReviveClient(argv, &task_params);

        #ifndef NDEBUG
        printf("\nwatch_process : i create new client\n");
        #endif
    }

    return 0;
}















