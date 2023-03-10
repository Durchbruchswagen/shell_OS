#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  /* newstate trzyma nowy stan procesu zwróconego przez waitpid, trzy flagi
  is_job służą do sprawdzenia czy trzeba zmienic status całego zadania*/
  int newstate = -1;
  bool is_job_finished = 1;
  bool is_job_stopped = 1;
  bool is_job_running = 1;
  for (int j = 0; j < njobmax; j++) {
    if (jobs[j].pgid == 0) {
      continue;
    }
    /*Wrapper do Waitpid ma jeden problem, mianowice dla ECHILD (oznacza że
    rodzic nie ma dzieci na które może czekać) kończy shella z błędem co jest
    niepotrzebne bo możemy po prostu wtedy zakończyć handler gdyż nie jest to
    błąd z którym nie możemy
    sobie poradzić (jest to nawet coś czego oczekujemy jeżeli przykładowo
    odpalimy tylko jeden proces pierwszoplanoyw)*/
    while ((pid = waitpid(-jobs[j].pgid, &status,
                          WNOHANG | WUNTRACED | WCONTINUED)) > 0) {

      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        newstate = FINISHED;
      } else if (WIFSTOPPED(status)) {
        newstate = STOPPED;
      } else if (WIFCONTINUED(status)) {
        newstate = RUNNING;
      }
      is_job_finished = 1;
      is_job_stopped = 1;
      is_job_running = 1;
      /*Przechodzimy po całym zadaniu, aktualizujemy flagi i stan, oraz exitcode
       * procesu*/
      for (int i = 0; i < jobs[j].nproc; i++) {
        if (jobs[j].proc[i].pid == pid) {
          jobs[j].proc[i].state = newstate;
          jobs[j].proc[i].exitcode = status;
        }
        if (jobs[j].proc[i].state == FINISHED) {
          is_job_stopped = 0;
          is_job_running = 0;
        }
        if (jobs[j].proc[i].state == RUNNING) {
          is_job_stopped = 0;
          is_job_finished = 0;
        }
        if (jobs[j].proc[i].state == STOPPED) {
          is_job_finished = 0;
          is_job_running = 0;
        }
      }
      if (is_job_stopped && jobs[j].state != STOPPED) {
        jobs[j].state = STOPPED;
      }
      if (is_job_running && jobs[j].state != RUNNING) {
        jobs[j].state = RUNNING;
      }
      if (is_job_finished && jobs[j].state != FINISHED) {
        jobs[j].state = FINISHED;
      }
    }
    /*Jeżeli dostaniemy inny error niż ECHILD to chcemy zakończyć program z
     * błędem*/
    if (pid == -1 && errno != 10) {
      unix_error("Waitpid error");
    }
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  /*Zapisujemy exitcode i usuwamy zadanie*/
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT
  /*Jeżeli proces ma zostać pierwszoplanowym musimy oddać terminal zadaniu żeby
   * zaraz po SIGCONT nie dostał sygnału który go zatrzyma*/
  if (!bg) {
    /*Przywracamy ustawienia terminala*/
    setfgpgrp(jobs[j].pgid);
    Tcsetattr(tty_fd, TCSAFLUSH, &jobs[j].tmodes);
  }
  Kill(-jobs[j].pgid, SIGCONT);
  printf("continue '%s'\n", jobcmd(j));
  if (!bg) {
    movejob(j, 0);
    while (jobs[0].state != RUNNING) {
      /*Czekamy aż zadanie stanie się running*/
      Sigsuspend(mask);
    }
    monitorjob(mask);
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  // Wysyłamy SIGTERM do zadania,jeżeli było ono zatrzymane musimy jeszcze
  // wysłać mu SIGCONT
  if (jobs[j].pgid > 0) {
    Kill(-jobs[j].pgid, SIGTERM);
    if (jobs[j].state == STOPPED) {
      Kill(-jobs[j].pgid, SIGCONT);
    }
  }
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    /*Sprawdzamy stan zadań i używamy makr żeby z pola status dostać kod
    wyjścia/numer sygnału i usuwamy zadanie jeżeli jego stan to finished*/
    if (which == jobs[j].state || which == ALL) {
      if (jobs[j].state == STOPPED) {
        printf("[%d] suspended '%s' \n", j, jobcmd(j));
      }
      if (jobs[j].state == RUNNING) {
        printf("[%d] running '%s'\n", j, jobcmd(j));
      }
      if (jobs[j].state == FINISHED) {
        int status = exitcode(&jobs[j]);
        if (WIFEXITED(status)) {
          printf("[%d] exited '%s', status=%d\n", j, jobcmd(j),
                 WEXITSTATUS(status));
        }
        if (WIFSIGNALED(status)) {
          printf("[%d] killed '%s' by signal %d\n", j, jobcmd(j),
                 WTERMSIG(status));
        }
        deljob(&jobs[j]);
      }
    }
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT
  state = RUNNING;
  while ((state = jobstate(0, &exitcode)) == RUNNING) {
    /*Monitorujemy czy zadanie dalej działa, w miedzyczasie możemy reagować na
     * SIGCHLD bo nie jest to krytyczna sekcja programu*/
    Sigsuspend(mask);
  }
  /*Jeżeli proces został zatrzymany to zapisujemy jego ustawienia terminala i
   * przesuwamy go na wolną pozycję*/
  if (state == STOPPED) {
    Tcgetattr(tty_fd, &jobs[0].tmodes);
    movejob(0, allocjob());
  }
  setfgpgrp(getpgid(getpid()));
  Tcsetattr(tty_fd, TCSAFLUSH, &shell_tmodes);
#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  int status = 0;
  pid_t pid;
  pid_t pgid;
  for (int i = 0; i < njobmax; i++) {
    if (jobs[i].pgid != 0) {
      pgid = jobs[i].pgid;
      killjob(i);
      /*Zabijamy wszystkie procesy w zadaniu i czekamy dopóki nie pogrzebiemy
       * wszystkich procesów*/
      while ((pid = waitpid(-pgid, &status, WNOHANG)) >= 0) {
        jobs[i].state = FINISHED;
        for (int j = 0; j < jobs[i].nproc; j++) {
          if (jobs[i].proc[j].pid == pid) {
            jobs[i].proc[j].state = FINISHED;
            jobs[i].proc[j].exitcode = status;
          }
        }
      }
      if (pid == -1 && errno != 10) {
        unix_error("Waitpid error");
      }
    }
  }
#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
