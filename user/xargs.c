/* 
Write a simple version of the UNIX xargs program for xv6: 
its arguments describe a command to run, 
it reads lines from the standard input, 
and it runs the command for each line, 
appending the line to the command's arguments. 
Your solution should be in the file user/xargs.c.
 */
#include "kernel/types.h"
#include "kernel/param.h" // where declares the max exec arguments MAXARG
#include "user/user.h"

int main(int argc, char* argv[]) {

    // argv[]: xargs command param1 param2 ...

    //* check the number of arguments
    if (argc < 2) {
        fprintf(2, "Usage: xargs minimum number of args is 2\n");
        exit(1);
    } else if (argc - 1 >= MAXARG){
        fprintf(2, "Usage: xargs maximum number of args is %d\n", MAXARG);
        exit(1);
    }

    //* extract the command and the original arguments
    char* arguments[MAXARG];
    int arg_pos;
    for (arg_pos = 0; arg_pos < argc - 1; arg_pos++) {
        arguments[arg_pos] = argv[arg_pos+1]; // command param1 param2 ...
    }

    char buf[1024]; // data readed from the standard input
    int input_size;

    char line_data[MAXARG]; // parse the input data of each line (seperate by '\n')
    char *line_arg = line_data;
    int data_pos = 0;

    //* read lines from the standard input and execute immediately in a loop
    while ( (input_size = read(0, buf, sizeof(buf))) ) {

        for (int idx = 0; idx < input_size; idx++) {

            if (buf[idx] == '\n') {

                line_data[data_pos] = 0; // end of a string
                arguments[arg_pos++] = line_arg; // append the input line to the command's arguments
                arguments[arg_pos] = 0;

                // reset the location variables
                line_arg = line_data;
                data_pos = 0;
                arg_pos = argc -1;

                // run the command in the child process & wait in the parent process
                int pid = fork();
                if (pid < 0) {
                    fprintf(2, "xargs: fork failed\n");
                    exit(1);
                } else if (pid == 0) {
                    exec(argv[1], arguments); 
                    fprintf(2, "xargs: exec failed\n");  // exec only returns when error occurs
                    exit(1);
                } else {
                    wait(0);
                }
                
            } else if (buf[idx] == ' ') { // NOTE: 其实也可以不用判断空格

                line_data[data_pos++] = 0;
                arguments[arg_pos++] = line_arg;
                line_arg = &line_data[data_pos];

            } else {

                line_data[data_pos++] = buf[idx];

            }
        }
    }

    exit(0);
}