#ifndef ASSERT_TEST_HELPER_H
#define ASSERT_TEST_HELPER_H

// 程序员违约测试工具层
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <signal.h>

#define EXPECT_ASSERT(expr)                                              \
  do {                                                                   \
    pid_t pid = fork();                                                  \
    if (pid == 0) {                                                      \
      expr;                                                              \
      fprintf(stderr,                                                    \
        "\n[EXPECT_ASSERT FAILED]\n"                                     \
        "  Location : %s:%d\n"                                           \
        "  Expr     : %s\n"                                              \
        "  Reason   : no assert triggered\n\n",                          \
        __FILE__, __LINE__, #expr);                                      \
      exit(0);                                                           \
    } else {                                                             \
      int status = 0;                                                    \
      waitpid(pid, &status, 0);                                          \
                                                                         \
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {               \
        fprintf(stderr,                                                  \
          "\n[EXPECT_ASSERT FAILED]\n"                                   \
          "  Location : %s:%d\n"                                         \
          "  Expr     : %s\n"                                            \
          "  Reason   : child exited normally\n\n",                      \
          __FILE__, __LINE__, #expr);                                    \
        assert(0);                                                       \
      }                                                                  \
                                                                         \
      if (WIFSIGNALED(status)) {                                         \
        int sig = WTERMSIG(status);                                      \
        if (sig != SIGABRT) {                                            \
          fprintf(stderr,                                                \
            "\n[EXPECT_ASSERT WARNING]\n"                                \
            "  Location : %s:%d\n"                                       \
            "  Expr     : %s\n"                                          \
            "  Signal   : %d (%s)\n"                                     \
            "  Note     : not an assert failure\n\n",                    \
            __FILE__, __LINE__, #expr, sig, strsignal(sig));             \
          assert(0);                                                     \
        }                                                                \
      }                                                                  \
    }                                                                    \
  } while (0)

#endif