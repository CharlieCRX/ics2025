#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

int printf(const char *fmt, ...) {
  panic("Not implemented");
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  panic("Not implemented");
}

int sprintf(char *out, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  char *p = out;
  while (*fmt != '\0') {
    if (*fmt != '%') {

      *p = *fmt;
      p++;
      fmt++;

    } else {
      fmt++;  // 跳过 '%'

      // 1. 整型变量
      if (*fmt == 'd') {
        int val = va_arg(args, int);
        int temp[20] = {0};
        int count = 0;

        if (val < 0) {
          *p = '-';
          p++;
          val = -val; // 杜绝后续除法结果为负数场景
        } else if (val == 0) {
          *p = '0';
          p++;
          continue; // 清晰逻辑
        }

        while (val) {
          temp[count] = val % 10;
          val = val / 10;
          count++;
        }

        while (count --) {
          *p = temp[count] + 48;
          p++;
        }

      } else if (*fmt == 's') {
        char *str = va_arg(args, char*);

        while (*str != '\0') {
          *p = *str;
          p++;
          str++;
        }
      }

      fmt++; // 自增到下一个字符
    }
  }

  *p = '\0';
  va_end(args);

  return p - out;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  panic("Not implemented");
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  panic("Not implemented");
}

#endif
