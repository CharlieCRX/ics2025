#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s) {
  const char *p = s;
  size_t len = 0;
  while (*p != '\0') {
    len++;
    p++;
  }
  return len;
}

char *strcpy(char *dst, const char *src) {
  const char *ptr_src = src;
  int count = 0;
  while(*ptr_src != '\0') {
    dst[count] = *ptr_src;
    count++;
    ptr_src++;
  }
  dst[count] = '\0';
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  // 1. 复制字符,直到遇到 '\0' 或者达到 n
  for (i = 0; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }

  // 2. 如果还没满 n 个，继续把剩下的位置补 '\0'
  for (; i < n; i++) {
    dst[i] = '\0';
  }

  return dst;
}


char *strcat(char *dst, const char *src) {
  char *ptr_dst = dst;
  const char *ptr_src = src;

  while (*ptr_dst != '\0')
  {
    ptr_dst++;
  }

  while (*ptr_src != '\0')
  {
    *ptr_dst = *ptr_src;
    ptr_dst++;
    ptr_src++;
  }
  *ptr_dst = '\0';
  
  return dst;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
  }
  return ((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 != '\0' && *s1 == *s2) {
    n--;
    s1++;
    s2++;
  }
  return (n == 0) ? 0 : ((unsigned char)*s1 - (unsigned char)*s2);
}

void *memset(void *s, int c, size_t n) {
  unsigned char *ptr = (unsigned char*)s;
  while (n--) {
    *ptr = (unsigned char)c;
    ptr++;
  }

  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *ptr_dst = dst;
  const unsigned char *ptr_src = src;

  if (ptr_dst < ptr_src) {
    for (size_t i = 0; i < n; i++) {
      ptr_dst[i] = ptr_src[i];
    }
  } else {
    while(n--) {
      ptr_dst[n] = ptr_src[n];
    }
  }
  return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *ptr_dst = dst;
  const unsigned char *ptr_src = src;

  for (size_t i = 0; i < n; i++) {
    ptr_dst[i] = ptr_src[i];
  }
  return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *ptr_s1 = s1;
  const unsigned char *ptr_s2 = s2;
  while (n != 0 && *ptr_s1 == *ptr_s2) {
    ptr_s1++;
    ptr_s2++;
    n--;
  }
  return (n == 0) ? 0 : (*ptr_s1 - *ptr_s2);
}

#endif
