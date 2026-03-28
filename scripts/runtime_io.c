/**
 * 课程要求的 5 个 IO 函数实现，与 LLVM IR 中的 declare 对应。
 * 供 test_llvm.sh / run_gt_llvm.sh 链接使用。
 */
#include <stdio.h>
#undef getchar

int getint(void) {
    int t;
    if (scanf("%d", &t) != 1)
        return 0;
    while (getc(stdin) != '\n') {}
    return t;
}

int getchar(void) {
    int c = getc(stdin);
    return (c == EOF) ? 0 : (unsigned char)c;
}

void putint(int x) {
    printf("%d", x);
}

void putch(int x) {
    printf("%c", (char)(x & 0xff));
}

void putstr(const char* s) {
    if (s)
        printf("%s", s);
}
