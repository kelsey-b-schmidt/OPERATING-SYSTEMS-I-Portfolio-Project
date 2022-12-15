#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#define MAXINPUT 2048 //shell must support command lines with a maximum length of 2048 characters
#define MAXARGS 512 //and a maximum of 512 arguments

// establish a 0/1 int to act as a boolean for whether foreground only mode is off or on
int foreground_only = 0;

// create processes array of for storing background processes
int processes[5];

// signal handler for SIGINT
// referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-signal-handling-api?module_item_id=22511478
/* Our signal handler for SIGINT */
void handle_SIGTSTP(int signo){
	  if (!foreground_only) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 49);
        fflush(stdout);
        foreground_only = 1;
    }
    else {
        char* message = "Exiting foreground-only mode (& is now allowed)\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 48);
        fflush(stdout);
        foreground_only = 0;
    }
}

int main(){
    // set up signal handling/ignoring stuff,
    // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-signals-concepts-and-types?module_item_id=22511477
    // and https://canvas.oregonstate.edu/courses/1890465/pages/exploration-signal-handling-api?module_item_id=22511478
    // and assignment specs at https://canvas.oregonstate.edu/courses/1890465/assignments/8990536?module_item_id=22511473
	 
    // per assignment specs, SIGINT (ctrl-C) must be:
    // ignored by the parent process (smallsh), 
    // ignored by background processes ("sleep 5 &", etc. must NOT be killed by ^C)
    // and processed normally by foreground processes ("sleep 5", etc. must BE killed by ^C, but not exit entirely out of smallsh)
    // additionally, "If a child foreground process is killed by a signal, the parent must immediately print out the number of the signal 
    // that killed it's foreground child process, before prompting the user for the next command."
    // see fork if/else black further down for rest of the ^C ignoring stuff

    // ignore ^C in parent process
    // Initialize SIGINT_action_action struct to be empty
    struct sigaction SIGINT_action = {0};
    // Fill out the SIGINT_action struct
    // Register SIG_IGN as the signal handler
    SIGINT_action.sa_handler = SIG_IGN;
    // Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
    // No flags set
    SIGINT_action.sa_flags = 0;
    // Install our signal handler
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Per assignment specs, for handling SIGTSTP (ctrl-Z): 
    // A child, if any, running as a foreground process must ignore SIGTSTP.
    // Any children running as background process must ignore SIGTSTP.
    // When the parent process running the shell receives SIGTSTP: 
    // The shell must display an informative message immediately if it's sitting at the prompt, 
    // or immediately after any currently running foreground process has terminated
    // The shell then enters a state where subsequent commands can no longer be run in the background.
    // In this state, the & operator should simply be ignored, i.e., all such commands are run as if they were foreground processes.
    // If the user sends SIGTSTP again, then your shell will display another informative message 
    // immediately after any currently running foreground process terminates.
    // The shell then returns back to the normal condition where the & operator is once again honored for subsequent commands, 
    // allowing them to be executed in the background.
    // see fork if/else black further down for rest of the ^Z handling stuff

    // catch ^Z in parent process
    // Initialize SIGTSTP_action struct to be empty
    struct sigaction SIGTSTP_action = {0};
    // Fill out the SIGTSTP_action struct
  	// Register handle_SIGTSTP as the signal handler
	  SIGTSTP_action.sa_handler = handle_SIGTSTP;
	  // Block all catchable signals while handle_SIGTSTP is running
	  sigfillset(&SIGTSTP_action.sa_mask);
	  // No flags set
  	SIGTSTP_action.sa_flags = 0;
    // Install our signal handler
	  sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // get process_id
    // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-concept-and-states?module_item_id=22511467
    pid_t process_id = getpid();
    // set a foreground exit status code to be used later when status is called
    int foreground_exit_status = 0;
    // and a background_exit_status, for background processes (checked before input)
    int background_exit_status = 0;
    
    for(;;){
        // just before prompt, check for terminated child processes and print a message if any terminated
        // set up a new pid_t "terminatedChild", and call waitpid() on each potential member of processes array
        // (so it will wait for any child process there),
        // have it put the exit status in background_exit_status,
        // and don't wait for this to finish, set it to WNOHANG, 
        // this will return the pid of any terminated processes into the terminatedChild pid_t, 
        // which we can then print, 
        // then we can print the exit info (similar to calling "status" from the command line)
        // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-monitoring-child-processes?module_item_id=22511469
        // and https://man7.org/linux/man-pages/man3/wait.3p.html
        pid_t terminatedChild;
        for (int j = 0; j < 5; j++) {
            if (processes[j]){
                while ((terminatedChild = waitpid(processes[j], &background_exit_status, WNOHANG)) > 0) {
                    printf("Background pid %d is done: ", terminatedChild);
                    fflush(stdout);
                    // get exit info from WIFEXITED/WEXITSTATUS, 
                    // this is basically identical to the "status" command below
                    // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-monitoring-child-processes?module_item_id=22511469
                    if(WIFEXITED(background_exit_status)){
                        printf("exit value %d\n", WEXITSTATUS(background_exit_status));
                        fflush(stdout);
                    } 
                    else{
                        printf("terminated by signal %d\n", WTERMSIG(background_exit_status));
                        fflush(stdout);
                    }
                    processes[j] = 0;
                }
            }
        }

        // print prompt
        printf(": ");
        fflush(stdout);

        // now we need to get the input from stdin
        // set up a buffer big enough to hold 2048 characters, plus null terminator, plus potential newline character
        // initialized with 0's, so we don't accidentally underwrite on next loop
        // referenced from Base64 assignment
        char string[MAXINPUT+2] = {0};
        
        // get input from stdin and put it into string
        fgets(string, MAXINPUT+2, stdin);
        
        // Clean up potential trailing newline character left by fgets, null terminate instead
        for (int j = 0; j < MAXINPUT+2; j++) {
            if (string[j] == '\n'){
                string[j] = '\0';
            }
        }
        
        // if empty input or a comment line is received, we don't have to do anything
        // we can go back to asking for input
        if ((strcmp(string, "") == 0) || (string[0] == '#')){
            continue;
        }

        // if exit input received, kill any leftover processes and exit input loop
        else if (strcmp(string, "exit") == 0){
            for(int j = 0; j < 5; j++) {
                if (processes[j]){
                    kill(processes[j], SIGTERM);
                }
            }
            break;
        }
        
        // otherwise process the input
        else{
            // create an array of pointers to strings, to hold the arguments,
            // filled with NULL, so it's null terminated later
            // setup referenced from https://overiq.com/c-programming-101/array-of-pointers-to-strings-in-c/
            // to facilitate execvp() later, which takes char *const argv[]
            // per https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-executing-a-new-program?module_item_id=22511470
            // set an i counter at 0 to fill this array later
            // create input_file and output_file string arrays
            // and create a 0/1 int to act as a boolean for whether the command should be a background process
            char* args[MAXARGS];
            for (int j=0; j<MAXARGS; j++) {
                args[j] = NULL;
            }
            int i = 0;
            char input_file[MAXINPUT+2] = {0}; 
            char output_file[MAXINPUT+2] = {0};
            int background_process = 0;
            
            // break up string
            // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-strings?module_item_id=22511451
            // and https://www.youtube.com/watch?v=34DnZ2ewyZo
            char* token = strtok(string, " ");
            while (token != NULL){
            
                // check for input file
                if (!strcmp(token, "<")){
                    // manually jump ahead one token to file name
                    token = strtok(NULL, " ");
                    strcpy(input_file, token);
                }
                
                // check for output file
                else if (!strcmp(token, ">")){
                    // manually jump ahead one token to file name
                    token = strtok(NULL, " ");
                    strcpy(output_file, token);
                } 
                
                // otherwise put it in the argument array
                else{
                    args[i] = strdup(token);
                    // get length of string we just copied
                    int length = 0;
                    while (args[i][length] != '\0') {
                        length++;
                    }

                    // expand $$ to process_id
                    // search through arg we just added for "$$""
                    for (int j = 0; j < length; j++) {
                        if ((args[i][j] == '$') && (args[i][j+1] == '$')){
                            // since in the test script $$ only happens at the end of the given argument,
                            // when $$ found, null terminate string at first $ (cutting off $$),
                            // use snprintf to write the pid into a string array,
                            // and concatenate that pid_string to args[i]
                            // snprintf usage referenced from https://edstem.org/us/courses/29177/discussion/2053610
                            args[i][j] = '\0';
                            char pid_string[MAXINPUT+2] = {0};
                            snprintf(pid_string, MAXINPUT+2, "%jd", (intmax_t) process_id);
                            strncat(args[i], pid_string, MAXINPUT+2-j);
                        }
                    } 
                    // go to next spot in args array
                    i++;
                }

                // get next token
                token = strtok(NULL, " ");
            }

            // check for background process flag "&"
            for (int j = 0; j < 500; j++){
                if (args[j] && (strcmp(args[j], "&") == 0) && !args[j+1]){
                    // set background_process to 1
                    background_process = 1;
                    // clear extra argument
                    args[j] = NULL;
                    break;
                }
            }

            // implement cd
            if (strcmp(args[0], "cd") == 0){
                fflush(stdout);
                // if an argument after cd is provided, change to that directory
                // adapted from Tree assignment
                // and https://canvas.oregonstate.edu/courses/1890465/pages/exploration-environment?module_item_id=22511471
                if (args[1]){
                    fflush(stdout);
                    if (chdir(args[1]) == -1){
                        printf("[could not open directory %s]\n", args[1]);
                        fflush(stdout);
                    }
                fflush(stdout);
                }
                else{
                    // otherwise go to main in HOME from environment
                    // per assignment specs https://canvas.oregonstate.edu/courses/1890465/assignments/8990536?module_item_id=22511473
                    if (chdir(getenv("HOME")) == -1){
                        printf("[could not open home directory]\n");
                        fflush(stdout);
                    }
                }
            }

            // implement status
            else if (strcmp(args[0], "status") == 0){
                // ignore '&' command after status for the "status &" test, per directions:
                // "If the user tries to run one of these built-in commands in the background with the & option, 
                // ignore that option and run the command in the foreground anyway 
                // (i.e. don't display an error, just run the command in the foreground)."
                // get exit info from WIFEXITED/WEXITSTATUS, 
                // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-monitoring-child-processes?module_item_id=22511469
                if(WIFEXITED(foreground_exit_status)){
                    printf("exit value %d\n", WEXITSTATUS(foreground_exit_status));
                    fflush(stdout);
                } 
                else{
                    printf("terminated by signal %d\n", WTERMSIG(foreground_exit_status));
                    fflush(stdout);
                }
            }

            // otherwise, if not a built in command, run a new process
            else{
                // we need to fork off a process
                // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-creating-and-terminating-processes?module_item_id=22511468
                // and https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-monitoring-child-processes?module_item_id=22511469
                // and https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-executing-a-new-program?module_item_id=22511470
                
                // Fork a new process
                pid_t childPid = fork();

                if (childPid == -1){
                    // only happens if fork process fails
                    printf("[fork failed]\n");
                    fflush(stdout);
                    exit(1);
                }
                else if (childPid == 0){
                    // Child process runs this code
                    
                    // continue to ignore ^C for background processes,
                    // SIG_IGN will continue past execvp call
                    // for foreground processes we need to set behavior back to default (SIG_DFL), 
                    if (!background_process || foreground_only){  
                        // Register SIG_DFL as the signal handler
                        SIGINT_action.sa_handler = SIG_DFL;
                        // Install our signal handler
                        sigaction(SIGINT, &SIGINT_action, NULL);
                    }

                    // ignore ^Z for all child processes,
                    // SIG_IGN will continue past execvp call
                    // Register SIG_IGN as the signal handler
                    SIGTSTP_action.sa_handler = SIG_IGN;
                    // Install our signal handler
                    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

                    // Handle input/output files
                    // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-processes-and-i-slash-o?module_item_id=22511479
                    if (strcmp(input_file, "")){
                        // Open source file
                        int sourceFD = open(input_file, O_RDONLY);
                        if (sourceFD == -1) {
                            fprintf(stderr, "smallsh: ");
                            fflush(stderr);
                            perror(input_file);
                            exit(1); 
                        }

                        // Redirect stdin to source file
                        int result = dup2(sourceFD, 0);
                        if (result == -1) { 
                            printf("[input file redirection failed]\n");
                            fflush(stdout);
                            exit(2); 
                        }
                    }

                    if (strcmp(output_file, "")){
                        // Open target file
                        int targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (targetFD == -1) { 
                            printf("[output file open failed]\n");
                            fflush(stdout);
                            exit(1); 
                        }
                    
                        // Redirect stdout to target file
                        int result = dup2(targetFD, 1);
                        if (result == -1) { 
                            printf("[output file redirection failed]\n");
                            fflush(stdout);
                            exit(2); 
                        }
                    }

                    execvp(args[0], args);
                    // execvp only returns if it fails, so next lines will only run in that case
                    fprintf(stderr, "smallsh: ");
                    fflush(stderr);
                    perror(args[0]);
                    if (strcmp(input_file, "")){
                        perror(input_file);
                    }
                    if (strcmp(output_file, "")){
                        perror(output_file);
                    }
                    exit(2);
                }
                else {
                    // parent process runs this code  

                    // if process is supposed to run in the background, 
                    // and we are not in foreground_only mode, 
                    // run with WNOHANG
                    // referenced from https://canvas.oregonstate.edu/courses/1890465/pages/exploration-process-api-monitoring-child-processes?module_item_id=22511469
                    // and add process id to processes array for checking later
                    if (background_process && !foreground_only){
                        printf("Background pid is %d\n", childPid);
                        fflush(stdout);
                        for(int j=0; j < 5; j++){
                            if(!processes[j]){
                                processes[j] = childPid;
                                break;
                            }
                        }
                        // use background_exit_status int from earlier to track status in main program, so we can print with status command
                        childPid = waitpid(childPid, &background_exit_status, WNOHANG);
                    }
                    else{ 
                        
                        // wait for child's termination
                        // use foreground_exit_status int from earlier to track status in main program, so we can print with status command
                        childPid = waitpid(childPid, &foreground_exit_status, 0);
                        // since we are running a foreground process, we need to print a message if it is terminated by a signal
                        if(!WIFEXITED(foreground_exit_status)){
                            printf("terminated by signal %d\n", WTERMSIG(foreground_exit_status));
                            fflush(stdout);
                        } 
                    }
                } 
            }    
        }
    }

    return 0;
}

