#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    /*Przechodzimy po całej liście tokenów i jeżeli napotkamy T_INPUT albo
    T_OUTPUT to zapamiętujemy token usuwamy go z listy i traktujemy następny
    token jako nazwę pliku.*/
    if (token[i] == T_INPUT) {
      mode = token[i];
      token[i] = T_NULL;
    } else if (token[i] == T_OUTPUT) {
      mode = token[i];
      token[i] = T_NULL;
    } else {
      if (mode != NULL) {
        if (mode == T_INPUT) {
          MaybeClose(inputp);
          *inputp = Open(token[i], O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        }
        if (mode == T_OUTPUT) {
          MaybeClose(outputp);
          *outputp = Open(token[i], O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        }
        token[i] = T_NULL;
        mode = NULL;
      }
      token[n] = token[i];
      if (n < i) {
        token[i] = T_NULL;
      }
      n += 1;
    }
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  pid_t pid;
  int j;
  if (!(pid = Fork())) { // child
    /*Ze względu na to jak działa wrapper to setpgid musimy sprawdzać czy grupa
    nie jest już ustawiona,
    gdyż jeżeli spróbujemy to zrobić dwa razy dostaniemy error od wrappera*/
    if (getpgid(getpid()) != getpid()) {
      Setpgid(0, 0);
    }
    /*Jeżeli odpalamy program jako pierwszoplanowy to każemy mu poczekać do
     * momentu w którym nie oddamy mu terminala*/
    if (!bg) {
      sigsuspend(&mask);
    }
    /*execve przywraca domyślną dyspozycję flag które nie były ignorowane w
     * rodzicu*/
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    if (input != -1) {
      dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }
    external_command(token);
  }
  if (getpgid(pid) != pid) {
    Setpgid(pid, pid);
  }
  MaybeClose(&input);
  MaybeClose(&output);
  // Tworzymy nowe zadanie i jeżeli jest pierwszoplanowe to je monitorujemy, w
  // przeciwnym wypadku wypisujemy tylko komunikat
  j = addjob(pid, bg);
  addproc(j, pid, token);
  if (bg) {
    printf("[%d] running '%s'\n", j, jobcmd(j));
  } else if (!bg) {

    setfgpgrp(pid);
    /*Wysyłamy dziecku sygnał dając mu znać że może kontynuować*/
    Kill(-pid, SIGCHLD);
    exitcode = monitorjob(&mask);
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  /*Działa tak samo jak do_job z tym wyjątkie że dostajemy grupę do której
  trzeba przypisać dziecko,a jeżeli jest on równy zero to jest on pierwszym
  procesem w grupie */
  if (!pid) {
    if (pgid == 0) {
      if (getpgid(getpid()) != getpid()) {
        Setpgid(0, 0);
      }
    } else {
      if (pgid != getpid()) {
        Setpgid(getpid(), pgid);
      }
    }
    if (!bg) {
      sigsuspend(mask);
    }
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    if (input != -1) {
      dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }
    if (!bg) {
      if (builtin_command(token) >= 0) {
        exit(EXIT_SUCCESS);
      }
    }
    external_command(token);
  }
  if (pgid == 0) {
    if (getpgid(pid) != pid) {
      Setpgid(pid, pid);
    }
  } else {
    if (pgid != pid) {
      Setpgid(pid, pgid);
    }
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  int pocz = -1;
  /*pocz to indeks od którego zaczyna się proces składowy całego pipe-a*/
  for (int i = 0; i < ntokens; i++) {
    if (pocz == -1) {
      pocz = i;
    }
    /*Jeżeli napotkamy token T_PIPE oznacz to że mamy już wszystko co potrzeba
     * do odpalania jednego procesu składowego*/
    if (token[i] == T_PIPE) {
      if (pgid == 0) {
        pid = do_stage(pgid, &mask, input, output, token + pocz, i - pocz, bg);
        pgid = pid;
        job = addjob(pid, bg);
        addproc(job, pid, token + pocz);
        /*Jeżeli jest to pierwszy proces zamykamy write-end pipe-a oraz
         * ewentualnie input*/
        MaybeClose(&output);
        MaybeClose(&input);
        pocz = -1;
      } else {
        /*Jeżeli nie jest to pierwszy proce to zamykamy read-end poprzedniego
         * pipe-a i write-end aktualnego pipe-a*/
        pid = do_stage(pgid, &mask, input, output, token + pocz, i - pocz, bg);
        addproc(job, pid, token + pocz);
        MaybeClose(&input);
        MaybeClose(&output);
        pocz = -1;
      }
      input = next_input;
      mkpipe(&next_input, &output);
    }
  }
  pid = do_stage(pgid, &mask, input, -1, token + pocz, ntokens - pocz, bg);
  MaybeClose(&input);
  MaybeClose(&next_input);
  MaybeClose(&output);
  addproc(job, pid, token + pocz);
  if (!bg) {
    setfgpgrp(pgid);
    Kill(-pgid, SIGCHLD);
    exitcode = monitorjob(&mask);
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
