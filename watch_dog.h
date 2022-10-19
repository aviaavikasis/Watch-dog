#ifndef __ILRD_OL127_128_WATCH_DOG_H__
#define __ILRD_OL127_128_WATCH_DOG_H__

#include <stddef.h>

typedef enum
{
    WD_READY = 0,
    WD_FAIL = 1
}wd_status;

/*
DESCRIPTION : the function is enable to the
user to save his program in alive, even in 
case that program is fail. once the program
will call the function, it is save in alive.

PARAMS: char pointer to the program path,
threshold of attempts to communicate with
the program before the watch dog determine 
that the program died, time interval to 
try to communicate the program

RETURN : WD_FAIL or WD_READY
in additional, the user can to check all time
if the watch dog exist, by the WD_PID env variable.
*/
wd_status MakeMeImortal(char *client_argv[], size_t threshold, size_t interval);

/*
DESCRIPTION : stop the watchdog program
and let the user program to terminate or fail.
*/
void LetMeDie();

#endif /* __ILRD_OL127_128_WATCH_DOG_H__ */
