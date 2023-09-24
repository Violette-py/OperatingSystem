/* 
Write a simple version of the UNIX xargs program for xv6: 
its arguments describe a command to run, 
it reads lines from the standard input, 
and it runs the command for each line, 
appending the line to the command's arguments. 
Your solution should be in the file user/xargs.c.
 */
// FIXME: 每个库具体的作用是什么？用在了哪里？
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include <stdarg.h>

#include<kernel/param.h>

char buf[1024];

int main(int argc, char* argv[]) {

    //* 从标准输入读入数据
    int input_size = read(0, buf, sizeof(buf));

    //* 将数据分行存储

    // 由于 C语言 不支持动态数组，故需要先统计行数
    int line = 0;
    for (int i = 0; i < input_size ; i++) {
        if (buf[i] == '\n') {
            line++;
        }
    }

    char input_data[line][MAXARG]; // param.h中提示，命令参数最长为32字符

    for (int i = 0, j = 0, m = 0; m < input_size; m++) {
        if (buf[m] == '\n') {
            input_data[i][j] = '\0';
            i++;
            j = 0;
        } else {
            input_data[i][j++] = buf[m];
        }
    }

    //* 将数据分行拼接到 argv[2]后，并运行 // argv[0]是 xargs自身，argv[1]是 指定命令，且后续可能还有其余参数 -- 由argc确定
    //  示例：$ echo hello too | xargs echo bye

    char* argument[MAXARG];
    int j;
    for (j = 0; j < argc - 1; j++) {
        argument[j] = argv[j+1];
    }

    for (int i = 0; i < line; i++) {
        argument[j] = input_data[i];

        if (fork() == 0) {
            exec(argv[1], argument);
            exit(0);
        } else {
            wait(0);
        }
    }
    exit(0);
}
