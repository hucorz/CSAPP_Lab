/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/* 错误包装函数 */
pid_t Fork(void);
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int Sigemptyset(sigset_t *set);
int Sigfillset(sigset_t *set);
int Sigaddset(sigset_t *set, int signum);
int Sigdelset(sigset_t *set, int  signum);
int Sigismemeber(const sigset_t *set, int signum);
int Kill(pid_t pid, int sig);
int Execve(char *path, char** argv, char** environ);

/* 在信号处理程序中使用的安全 IO 函数 */
size_t sio_strlen(char s[]);
ssize_t sio_puts(char *s);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {
        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }
        // verbose = 1;
        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];
    int bg;
    pid_t pid;

    bg = parseline(cmdline, argv);
    if (argv[0] == NULL) return;    // 空命令

    sigset_t mask_child, mask_prev, mask_all;
    Sigemptyset(&mask_child);
    Sigaddset(&mask_child, SIGCHLD);
    Sigfillset(&mask_all);

    if(!builtin_cmd(argv)) {
        Sigprocmask(SIG_BLOCK, &mask_child, &mask_prev);   // BLOCK SIGCHILD
        if ((pid = Fork()) == 0) { // child
            Sigprocmask(SIG_SETMASK, &mask_prev, NULL);    // UNBLOCK SIGCHILD
            setpgid(0, 0);
            Execve(argv[0], argv, environ);
            exit(0);
        }

        int state = bg ? BG : FG;
        Sigprocmask(SIG_BLOCK, &mask_all, NULL);
        addjob(jobs, pid, state, cmdline);
        Sigprocmask(SIG_SETMASK, &mask_prev, NULL);        // UNBLOCK SIGCHILD

        if (!bg) {  // foreground job
            waitfg(fgpid(jobs));
        } else {   // background job
            printf("[%d] (%d) %s",pid2jid(pid), pid, cmdline);
        }
        
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    // printf("in builtin_cmd\n");
    char *cmd = argv[0];
    if (!strcmp(cmd, "quit")) {
        exit(0);
    } 
    if (!strcmp(cmd, "jobs")) {
        listjobs(jobs);
        return 1;
    }
    if (!strcmp(cmd, "bg") || !strcmp(cmd, "fg")) {
        do_bgfg(argv);
        return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    sigset_t mask_all, mask_prev;
    Sigfillset(&mask_all);

    struct job_t* job;
    char* id = argv[1];

    if (!id) { // 没有 pid 或者 %jid
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
    if (!((id[0] == '%' && atoi(&id[1])) || atoi(&id[0]))) {  // pid 或者 %jid 不正确
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    // get job object
    if (id[0] == '%'){
        int jid = atoi(&id[1]);
        job = getjobjid(jobs, jid);
    } else {
        int pid = atoi(id);
        job = getjobpid(jobs, pid);
    }

    if (!job) {
        if (id[0] == '%')
            printf("%%%s: No such job\n", &id[1]);
        else
            printf("(%s): No such process\n", &id[0]);
        return;
    }

    if (!strcmp(argv[0], "bg")) {  // background process
        Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
        job->state = BG;
        Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
        
        Kill(-(job->pid), SIGCONT);
        printf("[%d] (%d) %s",job->jid, job->pid, job->cmdline);
    } else if(!strcmp(argv[0], "fg")) {  // foreground process
        Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
        job->state = FG;
        Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
        
        Kill(-(job->pid), SIGCONT);
        waitfg(job->pid);
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    sigset_t mask_empty;
    Sigemptyset(&mask_empty);

    // 这里用 while 判断是否还有前台程序
    // 不能用 if，因为 sigsuspend 可能中途因为其他后台程序完成，需要运行信号处理程序而提前返回
    while (fgpid(jobs) != 0) {
        sigsuspend(&mask_empty);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    if (verbose) {
        printf("\nin sigchld_handler\n");
        listjobs(jobs);
    }
    
    int olderrno = errno, status;
    pid_t pid;
    sigset_t mask_all, mask_prev;
    Sigfillset(&mask_all);
    // 这里 waitpid 的 options 不能为 0，否则如果有后台进程，sh 会挂起等待后台进程结束
    // WNOHANG 需要配合其他的行为
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) { 
        Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
        if (WIFEXITED(status)) {  // 正常退出的进程
            deletejob(jobs, pid);
        } else if (WIFSIGNALED(status)) {
            // printf 要在 deletejob 后面，有点坑(
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            deletejob(jobs, pid);
        } else if(WIFSTOPPED(status)) {
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            struct job_t* j = getjobpid(jobs, pid);
            j->state = ST;
        }
        Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
    }
    errno = olderrno;

    if (verbose) {
        printf("out sigchld_handler\n");
        listjobs(jobs);
        printf("\n");
    }
    
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    if (verbose) {
        printf("\nsigint_handler\n");
    }
    
    int olderrno = errno;
    sigset_t mask_all, mask_prev;
    Sigfillset(&mask_all);

    Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
    pid_t pid = fgpid(jobs);  // jobs 是全局变量
    Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
    if (pid > 0) {
        Kill(-pid, SIGINT);
        /* shell 收到 int 信号后杀死前台进程，前台进程会向父进程即 shell 发送 sigchld 信号
        *  下面代码对应的操作应该放到 sigchld_handler 中处理，因为前台进程可能会收到不是来自 shell 的停止信号(test16)，从而 shell 只会收到一个 sigchld 信号
        *   sigtstp_handler 同理 */
        // struct job_t* j = getjobpid(jobs, pid);
        // printf("Job [%d] (%d) terminated by signal 2\n", j->jid, j->pid);
    }
    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    if (verbose) {
        printf("\nsigtstp_handler\n");
    }

    int olderrno = errno;
    sigset_t mask_all, mask_prev;
    Sigfillset(&mask_all);

    Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
    pid_t pid = fgpid(jobs); // jobs 是全局变量
    Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
    if (pid > 0) {
        /* shell 收到 tstp 信号后暂停前台进程，前台进程会向父进程即 shell 发送 sigchld 信号
        *  下面一些代码对应的操作应该放到 sigchld_handler 中处理，因为前台进程可能会收到不是来自 shell 的停止信号(test16)，从而 shell 只会收到一个 sigchld 信号
        *   sigtint_handler 同理 */
        // Sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
        // struct job_t* j = getjobpid(jobs, pid);
        // j->state = ST;
        // Sigprocmask(SIG_SETMASK, &mask_prev, NULL);
        Kill(-pid, SIGTSTP);
        // printf("Job [%d] (%d) stopped by signal 20\n", j->jid, j->pid);
    }
    errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/*******************
 *  错误包装函数
 *******************/
pid_t Fork(void) {
    pid_t pid;

    if ((pid = fork()) < 0)
        unix_error("Fork error");
    return pid;
}

int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    int ret;
    if ((ret = sigprocmask(how, set, oldset)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}
int Sigemptyset(sigset_t *set) {
    int ret;
    if ((ret = sigemptyset(set)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}

int Sigfillset(sigset_t *set) {
    int ret;
    if ((ret = sigfillset(set)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}

int Sigaddset(sigset_t *set, int signum) {
    int ret;
    if ((ret = sigaddset(set, signum)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}

int Sigdelset(sigset_t *set, int  signum) {
    int ret;
    if ((ret = sigdelset(set, signum)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}

int Sigismemeber(const sigset_t *set, int signum) {
    int ret;
    if ((ret = sigismember(set, signum)) < 0) 
        unix_error("Sigprocmask error");
    return ret;
}

int Kill(pid_t pid, int sig){
    int ret;
    if((ret = kill(pid, sig)) < 0){
        unix_error("Kill error");
    }
    return ret;
}

int Execve(char *path, char** argv, char** environ) {
    int ret;
    if ((ret = execve(path, argv, environ)) < 0) {
        unix_error(path);
    }
    return ret;
}

/************************************
 *  在信号处理程序中使用的安全 IO 函数 
 ***********************************/
size_t sio_strlen(char s[]){
    size_t i = 0;
    while (s[i] != '\0')
        ++i;
    return i;
}

ssize_t sio_puts(char *s) {
    return write(STDOUT_FILENO, s, sio_strlen(s));
}



