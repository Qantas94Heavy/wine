/*
 * Win32 processes
 *
 * Copyright 1996, 1998 Alexandre Julliard
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "process.h"
#include "module.h"
#include "file.h"
#include "global.h"
#include "heap.h"
#include "task.h"
#include "ldt.h"
#include "syslevel.h"
#include "thread.h"
#include "winerror.h"
#include "pe_image.h"
#include "task.h"
#include "server.h"
#include "debug.h"

static BOOL32 PROCESS_Signaled( K32OBJ *obj, DWORD thread_id );
static BOOL32 PROCESS_Satisfied( K32OBJ *obj, DWORD thread_id );
static void PROCESS_AddWait( K32OBJ *obj, DWORD thread_id );
static void PROCESS_RemoveWait( K32OBJ *obj, DWORD thread_id );
static void PROCESS_Destroy( K32OBJ *obj );

const K32OBJ_OPS PROCESS_Ops =
{
    PROCESS_Signaled,    /* signaled */
    PROCESS_Satisfied,   /* satisfied */
    PROCESS_AddWait,     /* add_wait */
    PROCESS_RemoveWait,  /* remove_wait */
    NULL,                /* read */
    NULL,                /* write */
    PROCESS_Destroy      /* destroy */
};

static DWORD PROCESS_InitialProcessID = 0;


/***********************************************************************
 *           PROCESS_Current
 */
PDB32 *PROCESS_Current(void)
{
    return THREAD_Current()->process;
}

/***********************************************************************
 *           PROCESS_Initial
 *
 * FIXME: This works only while running all processes in the same
 *        address space (or, at least, the initial process is mapped
 *        into all address spaces as is KERNEL32 in Windows 95)
 *
 */
PDB32 *PROCESS_Initial(void)
{
    return PROCESS_IdToPDB( PROCESS_InitialProcessID );
}

/***********************************************************************
 *           PROCESS_GetPtr
 *
 * Get a process from a handle, incrementing the PDB refcount.
 */
PDB32 *PROCESS_GetPtr( HANDLE32 handle, DWORD access, int *server_handle )
{
    return (PDB32 *)HANDLE_GetObjPtr( PROCESS_Current(), handle,
                                     K32OBJ_PROCESS, access, server_handle );
}


/***********************************************************************
 *           PROCESS_IdToPDB
 *
 * Convert a process id to a PDB, making sure it is valid.
 */
PDB32 *PROCESS_IdToPDB( DWORD id )
{
    PDB32 *pdb;

    if (!id) return PROCESS_Current();
    pdb = PROCESS_ID_TO_PDB( id );
    if (!K32OBJ_IsValid( &pdb->header, K32OBJ_PROCESS ))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return NULL;
    }
    return pdb;
}



/***********************************************************************
 *           PROCESS_BuildEnvDB
 *
 * Build the env DB for the initial process
 */
static BOOL32 PROCESS_BuildEnvDB( PDB32 *pdb )
{
    /* Allocate the env DB (FIXME: should not be on the system heap) */

    if (!(pdb->env_db = HeapAlloc(SystemHeap,HEAP_ZERO_MEMORY,sizeof(ENVDB))))
        return FALSE;
    InitializeCriticalSection( &pdb->env_db->section );

    /* Allocate startup info */
    if (!(pdb->env_db->startup_info = 
          HeapAlloc( SystemHeap, HEAP_ZERO_MEMORY, sizeof(STARTUPINFO32A) )))
        return FALSE;

    /* Allocate the standard handles */

    pdb->env_db->hStdin  = FILE_DupUnixHandle( 0 );
    pdb->env_db->hStdout = FILE_DupUnixHandle( 1 );
    pdb->env_db->hStderr = FILE_DupUnixHandle( 2 );
    FILE_SetFileType( pdb->env_db->hStdin,  FILE_TYPE_CHAR );
    FILE_SetFileType( pdb->env_db->hStdout, FILE_TYPE_CHAR );
    FILE_SetFileType( pdb->env_db->hStderr, FILE_TYPE_CHAR );

    /* Build the command-line */

    pdb->env_db->cmd_line = HEAP_strdupA( SystemHeap, 0, "kernel32" );

    /* Build the environment strings */

    return ENV_BuildEnvironment( pdb );
}


/***********************************************************************
 *           PROCESS_InheritEnvDB
 */
static BOOL32 PROCESS_InheritEnvDB( PDB32 *pdb, LPCSTR cmd_line, LPCSTR env,
                                    STARTUPINFO32A *startup )
{
    if (!(pdb->env_db = HeapAlloc(pdb->heap, HEAP_ZERO_MEMORY, sizeof(ENVDB))))
        return FALSE;
    InitializeCriticalSection( &pdb->env_db->section );

    /* Copy the parent environment */

    if (!ENV_InheritEnvironment( pdb, env )) return FALSE;

    /* Copy the command line */

    if (!(pdb->env_db->cmd_line = HEAP_strdupA( pdb->heap, 0, cmd_line )))
        return FALSE;

    /* Remember startup info */
    if (!(pdb->env_db->startup_info = 
          HeapAlloc( pdb->heap, HEAP_ZERO_MEMORY, sizeof(STARTUPINFO32A) )))
        return FALSE;
    *pdb->env_db->startup_info = *startup;

    /* Inherit the standard handles */
    if (pdb->env_db->startup_info->dwFlags & STARTF_USESTDHANDLES)
    {
        pdb->env_db->hStdin  = pdb->env_db->startup_info->hStdInput;
        pdb->env_db->hStdout = pdb->env_db->startup_info->hStdOutput;
        pdb->env_db->hStderr = pdb->env_db->startup_info->hStdError;
    }
    else
    {
        pdb->env_db->hStdin  = pdb->parent->env_db->hStdin;
        pdb->env_db->hStdout = pdb->parent->env_db->hStdout;
        pdb->env_db->hStderr = pdb->parent->env_db->hStderr;
    }

    return TRUE;
}


/***********************************************************************
 *           PROCESS_FreePDB
 *
 * Free a PDB and all associated storage.
 */
static void PROCESS_FreePDB( PDB32 *pdb )
{
    pdb->header.type = K32OBJ_UNKNOWN;
    if (pdb->handle_table) HANDLE_CloseAll( pdb, NULL );
    ENV_FreeEnvironment( pdb );
    if (pdb->heap && (pdb->heap != pdb->system_heap)) HeapDestroy( pdb->heap );
    if (pdb->load_done_evt) K32OBJ_DecCount( pdb->load_done_evt );
    if (pdb->event) K32OBJ_DecCount( pdb->event );
    DeleteCriticalSection( &pdb->crit_section );
    HeapFree( SystemHeap, 0, pdb );
}


/***********************************************************************
 *           PROCESS_CreatePDB
 *
 * Allocate and fill a PDB structure.
 * Runs in the context of the parent process.
 */
static PDB32 *PROCESS_CreatePDB( PDB32 *parent )
{
    PDB32 *pdb = HeapAlloc( SystemHeap, HEAP_ZERO_MEMORY, sizeof(PDB32) );

    if (!pdb) return NULL;
    pdb->header.type     = K32OBJ_PROCESS;
    pdb->header.refcount = 1;
    pdb->exit_code       = 0x103; /* STILL_ACTIVE */
    pdb->threads         = 1;
    pdb->running_threads = 1;
    pdb->ring0_threads   = 1;
    pdb->system_heap     = SystemHeap;
    pdb->parent          = parent;
    pdb->group           = pdb;
    pdb->priority        = 8;  /* Normal */
    pdb->heap            = pdb->system_heap;  /* will be changed later on */

    InitializeCriticalSection( &pdb->crit_section );

    /* Allocate the events */

    if (!(pdb->event = EVENT_Create( TRUE, FALSE ))) goto error;
    if (!(pdb->load_done_evt = EVENT_Create( TRUE, FALSE ))) goto error;

    /* Create the handle table */

    if (!HANDLE_CreateTable( pdb, TRUE )) goto error;

    return pdb;

error:
    PROCESS_FreePDB( pdb );
    return NULL;
}


/***********************************************************************
 *           PROCESS_Init
 */
BOOL32 PROCESS_Init(void)
{
    PDB32 *pdb;
    THDB *thdb;

    /* Initialize virtual memory management */
    if (!VIRTUAL_Init()) return FALSE;

    /* Create the system and SEGPTR heaps */
    if (!(SystemHeap = HeapCreate( HEAP_GROWABLE, 0x10000, 0 ))) return FALSE;
    if (!(SegptrHeap = HeapCreate( HEAP_WINE_SEGPTR, 0, 0 ))) return FALSE;

    /* Create the initial process and thread structures */
    if (!(pdb = PROCESS_CreatePDB( NULL ))) return FALSE;
    if (!(thdb = THREAD_Create( pdb, 0, FALSE, NULL, NULL, NULL, NULL ))) return FALSE;
    thdb->unix_pid = getpid();

    PROCESS_InitialProcessID = PDB_TO_PROCESS_ID(pdb);

    /* Remember TEB selector of initial process for emergency use */
    SYSLEVEL_EmergencyTeb = thdb->teb_sel;

    /* Create the environment DB of the first process */
    if (!PROCESS_BuildEnvDB( pdb )) return FALSE;

    /* Initialize the first thread */
    if (CLIENT_InitThread()) return FALSE;

    return TRUE;
}


/***********************************************************************
 *           PROCESS_Create
 *
 * Create a new process database and associated info.
 */
PDB32 *PROCESS_Create( NE_MODULE *pModule, LPCSTR cmd_line, LPCSTR env,
                       HINSTANCE16 hInstance, HINSTANCE16 hPrevInstance,
                       STARTUPINFO32A *startup, PROCESS_INFORMATION *info )
{
    DWORD size, commit;
    int server_thandle, server_phandle;
    UINT32 cmdShow = 0;
    THDB *thdb = NULL;
    PDB32 *parent = PROCESS_Current();
    PDB32 *pdb = PROCESS_CreatePDB( parent );
    TDB *pTask;

    if (!pdb) return NULL;
    info->hThread = info->hProcess = INVALID_HANDLE_VALUE32;

    /* Create the heap */

    if (pModule->module32)
    {
	size  = PE_HEADER(pModule->module32)->OptionalHeader.SizeOfHeapReserve;
	commit = PE_HEADER(pModule->module32)->OptionalHeader.SizeOfHeapCommit;
    }
    else
    {
	size = 0x10000;
	commit = 0;
        pdb->flags |= PDB32_WIN16_PROC;  /* This is a Win16 process */
    }
    if (!(pdb->heap = HeapCreate( HEAP_GROWABLE, size, commit ))) goto error;
    pdb->heap_list = pdb->heap;

    /* Inherit the env DB from the parent */

    if (!PROCESS_InheritEnvDB( pdb, cmd_line, env, startup )) goto error;

    /* Create the main thread */

    if (pModule->module32)
        size = PE_HEADER(pModule->module32)->OptionalHeader.SizeOfStackReserve;
    else
        size = 0;
    if (!(thdb = THREAD_Create( pdb, size, FALSE, &server_thandle, &server_phandle,
                                NULL, NULL ))) goto error;
    if ((info->hThread = HANDLE_Alloc( parent, &thdb->header, THREAD_ALL_ACCESS,
                                       FALSE, server_thandle )) == INVALID_HANDLE_VALUE32)
        goto error;
    if ((info->hProcess = HANDLE_Alloc( parent, &pdb->header, PROCESS_ALL_ACCESS,
                                        FALSE, server_phandle )) == INVALID_HANDLE_VALUE32)
        goto error;
    info->dwProcessId = PDB_TO_PROCESS_ID(pdb);
    info->dwThreadId  = THDB_TO_THREAD_ID(thdb);

#if 0
    thdb->unix_pid = getpid(); /* FIXME: wrong here ... */
#else
    /* All Win16 'threads' have the same unix_pid, no matter by which thread
       they were created ! */
    pTask = (TDB *)GlobalLock16( parent->task );
    thdb->unix_pid = pTask? pTask->thdb->unix_pid : THREAD_Current()->unix_pid;
#endif

    /* Create a Win16 task for this process */

    if (startup->dwFlags & STARTF_USESHOWWINDOW)
        cmdShow = startup->wShowWindow;

    pdb->task = TASK_Create( thdb, pModule, hInstance, hPrevInstance, cmdShow);
    if (!pdb->task) goto error;


    /* Map system DLLs into this process (from initial process) */
    /* FIXME: this is a hack */
    pdb->modref_list = PROCESS_Initial()->modref_list;
    

    return pdb;

error:
    if (info->hThread != INVALID_HANDLE_VALUE32) CloseHandle( info->hThread );
    if (info->hProcess != INVALID_HANDLE_VALUE32) CloseHandle( info->hProcess );
    if (thdb) K32OBJ_DecCount( &thdb->header );
    PROCESS_FreePDB( pdb );
    return NULL;
}


/***********************************************************************
 *           PROCESS_Signaled
 */
static BOOL32 PROCESS_Signaled( K32OBJ *obj, DWORD thread_id )
{
    PDB32 *pdb = (PDB32 *)obj;
    assert( obj->type == K32OBJ_PROCESS );
    return K32OBJ_OPS( pdb->event )->signaled( pdb->event, thread_id );
}


/***********************************************************************
 *           PROCESS_Satisfied
 *
 * Wait on this object has been satisfied.
 */
static BOOL32 PROCESS_Satisfied( K32OBJ *obj, DWORD thread_id )
{
    PDB32 *pdb = (PDB32 *)obj;
    assert( obj->type == K32OBJ_PROCESS );
    return K32OBJ_OPS( pdb->event )->satisfied( pdb->event, thread_id );
}


/***********************************************************************
 *           PROCESS_AddWait
 *
 * Add thread to object wait queue.
 */
static void PROCESS_AddWait( K32OBJ *obj, DWORD thread_id )
{
    PDB32 *pdb = (PDB32 *)obj;
    assert( obj->type == K32OBJ_PROCESS );
    return K32OBJ_OPS( pdb->event )->add_wait( pdb->event, thread_id );
}


/***********************************************************************
 *           PROCESS_RemoveWait
 *
 * Remove thread from object wait queue.
 */
static void PROCESS_RemoveWait( K32OBJ *obj, DWORD thread_id )
{
    PDB32 *pdb = (PDB32 *)obj;
    assert( obj->type == K32OBJ_PROCESS );
    return K32OBJ_OPS( pdb->event )->remove_wait( pdb->event, thread_id );
}


/***********************************************************************
 *           PROCESS_Destroy
 */
static void PROCESS_Destroy( K32OBJ *ptr )
{
    PDB32 *pdb = (PDB32 *)ptr;
    assert( ptr->type == K32OBJ_PROCESS );

    /* Free everything */

    ptr->type = K32OBJ_UNKNOWN;
    PROCESS_FreePDB( pdb );
}


/***********************************************************************
 *           ExitProcess   (KERNEL32.100)
 */
void WINAPI ExitProcess( DWORD status )
{
    PDB32 *pdb = PROCESS_Current();
    TDB *pTask = (TDB *)GlobalLock16( pdb->task );
    if ( pTask ) pTask->nEvents++;

    if ( pTask && pTask->thdb != THREAD_Current() )
        ExitThread( status );

    SYSTEM_LOCK();
    /* FIXME: should kill all running threads of this process */
    pdb->exit_code = status;
    EVENT_Set( pdb->event );
    if (pdb->console) FreeConsole();
    SYSTEM_UNLOCK();

    __RESTORE_ES;  /* Necessary for Pietrek's showseh example program */
    TASK_KillCurrentTask( status );
}


/******************************************************************************
 *           TerminateProcess   (KERNEL32.684)
 */
BOOL32 WINAPI TerminateProcess( HANDLE32 handle, DWORD exit_code )
{
    int server_handle;
    BOOL32 ret;
    PDB32 *pdb = PROCESS_GetPtr( handle, PROCESS_TERMINATE, &server_handle );
    if (!pdb) return FALSE;
    ret = !CLIENT_TerminateProcess( server_handle, exit_code );
    K32OBJ_DecCount( &pdb->header );
    return ret;
}

/***********************************************************************
 *           GetCurrentProcess   (KERNEL32.198)
 */
HANDLE32 WINAPI GetCurrentProcess(void)
{
    return CURRENT_PROCESS_PSEUDOHANDLE;
}


/*********************************************************************
 *           OpenProcess   (KERNEL32.543)
 */
HANDLE32 WINAPI OpenProcess( DWORD access, BOOL32 inherit, DWORD id )
{
    int server_handle;
    PDB32 *pdb = PROCESS_ID_TO_PDB(id);
    if (!K32OBJ_IsValid( &pdb->header, K32OBJ_PROCESS ))
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return 0;
    }
    if ((server_handle = CLIENT_OpenProcess( pdb->server_pid, access, inherit )) == -1)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return 0;
    }
    return HANDLE_Alloc( PROCESS_Current(), &pdb->header, access,
                         inherit, server_handle );
}			      


/***********************************************************************
 *           GetCurrentProcessId   (KERNEL32.199)
 */
DWORD WINAPI GetCurrentProcessId(void)
{
    PDB32 *pdb = PROCESS_Current();
    return PDB_TO_PROCESS_ID( pdb );
}


/***********************************************************************
 *           GetProcessHeap    (KERNEL32.259)
 */
HANDLE32 WINAPI GetProcessHeap(void)
{
    PDB32 *pdb = PROCESS_Current();
    return pdb->heap ? pdb->heap : SystemHeap;
}


/***********************************************************************
 *           GetThreadLocale    (KERNEL32.295)
 */
LCID WINAPI GetThreadLocale(void)
{
    return PROCESS_Current()->locale;
}


/***********************************************************************
 *           SetPriorityClass   (KERNEL32.503)
 */
BOOL32 WINAPI SetPriorityClass( HANDLE32 hprocess, DWORD priorityclass )
{
    PDB32 *pdb = PROCESS_GetPtr( hprocess, PROCESS_SET_INFORMATION, NULL );
    if (!pdb) return FALSE;
    switch (priorityclass)
    {
    case NORMAL_PRIORITY_CLASS:
    	pdb->priority = 0x00000008;
	break;
    case IDLE_PRIORITY_CLASS:
    	pdb->priority = 0x00000004;
	break;
    case HIGH_PRIORITY_CLASS:
    	pdb->priority = 0x0000000d;
	break;
    case REALTIME_PRIORITY_CLASS:
    	pdb->priority = 0x00000018;
    	break;
    default:
    	WARN(process,"Unknown priority class %ld\n",priorityclass);
	break;
    }
    K32OBJ_DecCount( &pdb->header );
    return TRUE;
}


/***********************************************************************
 *           GetPriorityClass   (KERNEL32.250)
 */
DWORD WINAPI GetPriorityClass(HANDLE32 hprocess)
{
    PDB32 *pdb = PROCESS_GetPtr( hprocess, PROCESS_QUERY_INFORMATION, NULL );
    DWORD ret = 0;
    if (pdb)
    {
    	switch (pdb->priority)
        {
	case 0x00000008:
	    ret = NORMAL_PRIORITY_CLASS;
	    break;
	case 0x00000004:
	    ret = IDLE_PRIORITY_CLASS;
	    break;
	case 0x0000000d:
	    ret = HIGH_PRIORITY_CLASS;
	    break;
	case 0x00000018:
	    ret = REALTIME_PRIORITY_CLASS;
	    break;
	default:
	    WARN(process,"Unknown priority %ld\n",pdb->priority);
	}
	K32OBJ_DecCount( &pdb->header );
    }
    return ret;
}


/***********************************************************************
 *           GetStdHandle    (KERNEL32.276)
 *
 * FIXME: These should be allocated when a console is created, or inherited
 *        from the parent.
 */
HANDLE32 WINAPI GetStdHandle( DWORD std_handle )
{
    HFILE32 hFile;
    int fd;
    PDB32 *pdb = PROCESS_Current();

    switch(std_handle)
    {
    case STD_INPUT_HANDLE:
        if (pdb->env_db->hStdin) return pdb->env_db->hStdin;
        fd = 0;
        break;
    case STD_OUTPUT_HANDLE:
        if (pdb->env_db->hStdout) return pdb->env_db->hStdout;
        fd = 1;
        break;
    case STD_ERROR_HANDLE:
        if (pdb->env_db->hStderr) return pdb->env_db->hStderr;
        fd = 2;
        break;
    default:
        SetLastError( ERROR_INVALID_PARAMETER );
        return INVALID_HANDLE_VALUE32;
    }
    hFile = FILE_DupUnixHandle( fd );
    if (hFile != HFILE_ERROR32)
    {
        FILE_SetFileType( hFile, FILE_TYPE_CHAR );
        switch(std_handle)
        {
        case STD_INPUT_HANDLE:  pdb->env_db->hStdin  = hFile; break;
        case STD_OUTPUT_HANDLE: pdb->env_db->hStdout = hFile; break;
        case STD_ERROR_HANDLE:  pdb->env_db->hStderr = hFile; break;
        }
    }
    return hFile;
}


/***********************************************************************
 *           SetStdHandle    (KERNEL32.506)
 */
BOOL32 WINAPI SetStdHandle( DWORD std_handle, HANDLE32 handle )
{
    PDB32 *pdb = PROCESS_Current();
    /* FIXME: should we close the previous handle? */
    switch(std_handle)
    {
    case STD_INPUT_HANDLE:
        pdb->env_db->hStdin = handle;
        return TRUE;
    case STD_OUTPUT_HANDLE:
        pdb->env_db->hStdout = handle;
        return TRUE;
    case STD_ERROR_HANDLE:
        pdb->env_db->hStderr = handle;
        return TRUE;
    }
    SetLastError( ERROR_INVALID_PARAMETER );
    return FALSE;
}

/***********************************************************************
 *           GetProcessVersion    (KERNEL32)
 */
DWORD WINAPI GetProcessVersion( DWORD processid )
{
    TDB *pTask;
    PDB32 *pdb = PROCESS_IdToPDB( processid );

    if (!pdb) return 0;
    if (!(pTask = (TDB *)GlobalLock16( pdb->task ))) return 0;
    return (pTask->version&0xff) | (((pTask->version >>8) & 0xff)<<16);
}

/***********************************************************************
 *           GetProcessFlags    (KERNEL32)
 */
DWORD WINAPI GetProcessFlags( DWORD processid )
{
    PDB32 *pdb = PROCESS_IdToPDB( processid );
    if (!pdb) return 0;
    return pdb->flags;
}

/***********************************************************************
 *		SetProcessWorkingSetSize	[KERNEL32.662]
 * Sets the min/max working set sizes for a specified process.
 *
 * PARAMS
 *    hProcess [I] Handle to the process of interest
 *    minset   [I] Specifies minimum working set size
 *    maxset   [I] Specifies maximum working set size
 *
 * RETURNS  STD
 */
BOOL32 WINAPI SetProcessWorkingSetSize(HANDLE32 hProcess,DWORD minset,
                                       DWORD maxset)
{
    FIXME(process,"(0x%08x,%ld,%ld): stub - harmless\n",hProcess,minset,maxset);
    if(( minset == -1) && (maxset == -1)) {
        /* Trim the working set to zero */
        /* Swap the process out of physical RAM */
    }
    return TRUE;
}

/***********************************************************************
 *           GetProcessWorkingSetSize    (KERNEL32)
 */
BOOL32 WINAPI GetProcessWorkingSetSize(HANDLE32 hProcess,LPDWORD minset,
                                       LPDWORD maxset)
{
	FIXME(process,"(0x%08x,%p,%p): stub\n",hProcess,minset,maxset);
	/* 32 MB working set size */
	if (minset) *minset = 32*1024*1024;
	if (maxset) *maxset = 32*1024*1024;
	return TRUE;
}

/***********************************************************************
 *           SetProcessShutdownParameters    (KERNEL32)
 */
BOOL32 WINAPI SetProcessShutdownParameters(DWORD level,DWORD flags)
{
    FIXME(process,"(%ld,0x%08lx): stub\n",level,flags);
    return TRUE;
}

/***********************************************************************
 *           SetProcessPriorityBoost    (KERNEL32)
 */
BOOL32 WINAPI SetProcessPriorityBoost(HANDLE32 hprocess,BOOL32 disableboost)
{
    FIXME(process,"(%d,%d): stub\n",hprocess,disableboost);
    /* Say we can do it. I doubt the program will notice that we don't. */
    return TRUE;
}

/***********************************************************************
 *           ReadProcessMemory    		(KERNEL32)
 * FIXME: check this, if we ever run win32 binaries in different addressspaces
 *	  ... and add a sizecheck
 */
BOOL32 WINAPI ReadProcessMemory( HANDLE32 hProcess, LPCVOID lpBaseAddress,
                                 LPVOID lpBuffer, DWORD nSize,
                                 LPDWORD lpNumberOfBytesRead )
{
	memcpy(lpBuffer,lpBaseAddress,nSize);
	if (lpNumberOfBytesRead) *lpNumberOfBytesRead = nSize;
	return TRUE;
}

/***********************************************************************
 *           WriteProcessMemory    		(KERNEL32)
 * FIXME: check this, if we ever run win32 binaries in different addressspaces
 *	  ... and add a sizecheck
 */
BOOL32 WINAPI WriteProcessMemory(HANDLE32 hProcess, LPVOID lpBaseAddress,
                                 LPVOID lpBuffer, DWORD nSize,
                                 LPDWORD lpNumberOfBytesWritten )
{
	memcpy(lpBaseAddress,lpBuffer,nSize);
	if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nSize;
	return TRUE;
}

/***********************************************************************
 *           ConvertToGlobalHandle    		(KERNEL32)
 */
HANDLE32 WINAPI ConvertToGlobalHandle(HANDLE32 hSrc)
{
    HANDLE32 hProcessInit, hDest;

    /* Get a handle to the initial process */
    hProcessInit = OpenProcess( PROCESS_ALL_ACCESS, FALSE, PROCESS_InitialProcessID );

    /* Duplicate the handle into the initial process */
    if ( !DuplicateHandle( GetCurrentProcess(), hSrc, hProcessInit, &hDest,
                           0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE ) )
        hDest = 0;

    /* Close initial process handle */
    CloseHandle( hProcessInit );

    /* Return obfuscated global handle */
    return hDest? HANDLE_LOCAL_TO_GLOBAL( hDest ) : 0;
}

/***********************************************************************
 *           RegisterServiceProcess             (KERNEL, KERNEL32)
 *
 * A service process calls this function to ensure that it continues to run
 * even after a user logged off.
 */
DWORD WINAPI RegisterServiceProcess(DWORD dwProcessId, DWORD dwType)
{
	/* I don't think that Wine needs to do anything in that function */
	return 1; /* success */
}

/***********************************************************************
 * GetExitCodeProcess [KERNEL32.325]
 *
 * Gets termination status of specified process
 * 
 * RETURNS
 *   Success: TRUE
 *   Failure: FALSE
 * 
 * FIXME
 *   Should call SetLastError (but doesn't).
 */
BOOL32 WINAPI GetExitCodeProcess(
    HANDLE32 hProcess,  /* [I] handle to the process */
    LPDWORD lpExitCode) /* [O] address to receive termination status */
{
    PDB32 *process;
    int server_handle;
    struct get_process_info_reply info;

    if (!(process = PROCESS_GetPtr( hProcess, PROCESS_QUERY_INFORMATION,
                                    &server_handle )))
        return FALSE;
    if (server_handle != -1)
    {
        CLIENT_GetProcessInfo( server_handle, &info );
        if (lpExitCode) *lpExitCode = info.exit_code;
    }
    else if (lpExitCode) *lpExitCode = process->exit_code;
    K32OBJ_DecCount( &process->header );
    return TRUE;
}

/***********************************************************************
 * GetProcessHeaps [KERNEL32.376]
 */
DWORD WINAPI GetProcessHeaps(DWORD nrofheaps,HANDLE32 *heaps) {
	FIXME(win32,"(%ld,%p), incomplete implementation.\n",nrofheaps,heaps);

	if (nrofheaps) {
		heaps[0] = GetProcessHeap();
		/* ... probably SystemHeap too ? */
		return 1;
	}
	/* number of available heaps */
	return 1;
}

/***********************************************************************
 * PROCESS_SuspendOtherThreads
 */

void PROCESS_SuspendOtherThreads(void)
{
    PDB32 *pdb;
    THREAD_ENTRY *entry;

    SYSTEM_LOCK();

    pdb = PROCESS_Current();
    entry = pdb->thread_list->next;
    for (;;)
    {
         if (entry->thread != THREAD_Current() && !THREAD_IsWin16(entry->thread))
         {
             HANDLE32 handle = HANDLE_Alloc( PROCESS_Current(), 
                                             &entry->thread->header,
                                             THREAD_ALL_ACCESS, FALSE, -1 );
             SuspendThread(handle);
             CloseHandle(handle);
         }
         if (entry == pdb->thread_list) break;
         entry = entry->next;
    }

    SYSTEM_UNLOCK();
}

/***********************************************************************
 * PROCESS_ResumeOtherThreads
 */

void PROCESS_ResumeOtherThreads(void)
{
    PDB32 *pdb;
    THREAD_ENTRY *entry;

    SYSTEM_LOCK();

    pdb = PROCESS_Current();
    entry = pdb->thread_list->next;
    for (;;)
    {
         if (entry->thread != THREAD_Current() && !THREAD_IsWin16(entry->thread))
         {
             HANDLE32 handle = HANDLE_Alloc( PROCESS_Current(), 
                                             &entry->thread->header,
                                             THREAD_ALL_ACCESS, FALSE, -1 );
             ResumeThread(handle);
             CloseHandle(handle);
         }
         if (entry == pdb->thread_list) break;
         entry = entry->next;
    }

    SYSTEM_UNLOCK();
}

BOOL32 WINAPI Process32First(HANDLE32 hSnapshot, LPPROCESSENTRY32 lppe)
{
  FIXME (process, "(0x%08lx,0x%08lx), stub!\n", hSnapshot, lppe);
  SetLastError (ERROR_NO_MORE_FILES);
  return FALSE;
}

BOOL32 WINAPI Process32Next(HANDLE32 hSnapshot, LPPROCESSENTRY32 lppe)
{
  FIXME (process, "(0x%08lx,0x%08lx), stub!\n", hSnapshot, lppe);
  SetLastError (ERROR_NO_MORE_FILES);
  return FALSE;
}
 
