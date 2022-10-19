#include <stdlib.h> /* realpath */
#include <stdio.h>  /* printf */
#include <sys/types.h>
#include <unistd.h>
#include <asm/unistd.h> /* getpid */
#include <dlfcn.h>

#include "watch_dog.h"

#define TRUE 1
#define UNUSED(x) (void)x

typedef wd_status(*opfunc_mmi_ty)(char *[], size_t, size_t);
typedef void(*opfunc_let_mi_die_ty)();

int main(int argc, char *argv[])
{
	void *libhandle = NULL;
    char *librarypath = "/home/aviaavikasis/Documents/projects/WatchDog/libwatch.so";
	opfunc_mmi_ty mmi = NULL;
    opfunc_let_mi_die_ty let_mi_die = NULL;
    puts("This is a shared library test...");
    libhandle = dlopen(librarypath , RTLD_LAZY);
    
    if(libhandle == NULL)
    {
    	printf("the library not found\n");
    	perror("dlopen");
    }
   
    *(void **)(&mmi) = dlsym(libhandle , "MakeMeImortal");
    *(void **)(&let_mi_die) = dlsym(libhandle, "LetMeDie");

    mmi(argv, 3, 3);

    UNUSED(argc);
   
    printf("\nhi ,i am client\n");
    printf("\nclient pid : %d\n", getpid());
    
    printf("\n1\n");	

    while(TRUE)
    {

    }

    let_mi_die();
    dlclose(libhandle);

    return 0;
}
