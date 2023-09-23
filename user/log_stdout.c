#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include <stdarg.h>

char buf[1024];

int read_stdin(char* buf) {
    /*
    Description: Read stdin into buf
    Example:
        - read_stdin(); // Read the stdin into buf
    Parameters:
        - buf (char*): A buffer to store all characters
    Return:
        - 0 (int)
    */
    //* Your code here

    // Read data from standard input and store it in buf
    int n = 0;
    char c;
    while (read(0, &c, 1) > 0) {
        buf[n++] = c;
    }
    buf[n] = '\0';

    //* End

    return 0;
}

int log_stdout(uint i) {
    /*
    Description: Redirect stdout to a log file named i.log
    Example:
        - log_stdout(1); // Redirect the stdout to 1.log and return 0
    Parameters:
        - i (uint): A number
    Return:
        - 0 (int)
    */
    char log_name[15] = "0.log";

    //* Your code here

    if (i != 0) {
        // Calculate the number of digits in 'i' to determine the file name length
        uint digits = 0;
        uint tempNumber = i;
        while (tempNumber != 0) {
            digits++;
            tempNumber /= 10;
        }
        // Construct the file name from right to left by filling in digits as characters
        for (uint currentDigit = 0; currentDigit < digits; currentDigit++) {
            log_name[digits - currentDigit - 1] = '0' + (i % 10); // convert uint to char
            i /= 10;
        }
        // Append the file extension ".log"
        strcpy(log_name + digits, ".log"); // char *strcpy(char *dest, const char *src);
    }

    // Close the standard output file descriptor
    close(1);

    // Open the file and use it as the new standard output (the original standard output was to the terminal, now it's writing to the file)
    if (open(log_name, O_CREATE | O_WRONLY) != 1) { // Create the file if it doesn't exist, open it in write-only mode, and set its file descriptor to 1
        fprintf(2, "log_stdout: open failed\n");
        return -1;
    }

    //* End

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(2, "Usage: log_stdout number\n");
        exit(1);
    }

    // Pay attention to the order of function calls: first log_stdout, then read_stdin, and finally printf

    if (log_stdout(atoi(argv[1])) != 0) {
        fprintf(2, "log_stdout: log_stdout failed\n"); 
        exit(1);
    }
    if (read_stdin(buf) != 0) {
        fprintf(2, "log_stdout: read_stdin failed\n"); 
        exit(1);
    }
    printf(buf);  // Since log_stdout redirected standard output to a .log file, this printf's content won't be displayed on the terminal but will be written to the file
    exit(0);
}
