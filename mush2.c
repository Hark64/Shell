#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include "mush.h"

void execInput(pipeline commands);
void prompt(FILE *infile);
void handler(int signum);
void cd(struct clstage *args);

int main(int argc, char *argv[]){
	FILE *infile = stdin;
	struct sigaction sa;

	/*crash  if too many args*/
	if(argc > 2){
		fprintf(stderr, "usage: mush2 [file]");
		exit(EXIT_FAILURE);
	}

	/*read from given file if one is given*/
	if(argc == 2){
		infile = fopen(argv[1], "r");
		if(infile == NULL){
			perror("fopen");
			exit(EXIT_FAILURE);
		}
	}

	/*set up signal handler for SIGINT*/
	sa.sa_handler = handler;
	if(sigemptyset(&sa.sa_mask) == -1){
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	sa.sa_flags = 0;
	if(sigaction(SIGINT, &sa, NULL) == -1){
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
		
	prompt(infile);

	return 1;
}

/*takes in the given file if one was given, otherwise infile
 * will be stdin*/
void prompt(FILE *infile){
	int ttys = 0;
	char *input = NULL;
	pipeline commands = NULL;
	int batchMode = 0;

	/*if infile is not stdin, then we are in batch mode*/
	if(infile != stdin){
		batchMode = 1;
	}

	/*check is stdin and stdout are ttys*/
	if( isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)){
		ttys = 1;
	}

	/*keep reading user input until an eof has been hit*/
	for(;;){
		/*only print prompt if ttys and not in batch mode*/
		if(ttys && !batchMode){
			printf("8-P ");
			fflush(infile);
		}

		/*get input and check if it's usable*/
		if((input = readLongString(infile))==NULL || input[0] =='\0'){
			yylex_destroy();
		
			/*close if end of file*/
			if(feof(infile)){
				fclose(infile);
				exit(EXIT_SUCCESS);
			}
			/*reprompt if sigint*/
			/*in batch mode, close the file and the shell*/
			if(ferror(infile)){
				if(!batchMode){
					if(ttys){
						printf("\n");
						fflush(infile);
					}	
					continue;
				}else{
					fclose(infile);
					exit(EXIT_SUCCESS);
				}
			}
			/*if the user only hit enter with no text, reprompt*/
			free(input);
			continue;
		}
	
		/*break the input into a pipeline and execute it w/ execInput*/
		yylex_destroy();
		commands = crack_pipeline(input);
		/*if ambiguous input/output crack_pipeline returns null*/
		if(commands != NULL){
			execInput(commands);
		}
		free(input);
		free_pipeline(commands);
	}
	return;
}


void execInput(pipeline commands){
	struct clstage *current = NULL; 
	int *fds = NULL;
	int i = 0, j=0, wstatus = 0, fdi, fdo, plength = 0;
	pid_t child = 0;
	sigset_t block_set;

	/*set variables that help keep track of current place in the pipeline*/
	plength = commands->length; /*pipline length*/
	current = commands->stage;  /*current place in pipeline*/

	/*run cd natively*/
	if( strcmp("cd", current->argv[0]) == 0){
		cd(current);
		return;
	}

	/*pipes only need to be made if the pipeline length is greater than 1*/
	if(plength != 1){
		/*allocate space for the fds for each pipe*/
		fds = malloc( (2*(plength-1)) * sizeof(int));
		if(fds == NULL){
			perror("malloc");
			exit(EXIT_FAILURE);	
		}
		/*make pipes*/
		i = 0;
		while(i< (plength-1)){
			if(pipe(fds+(i*2))==-1){
				perror("malloc");
				exit(EXIT_FAILURE);
			}
			i++;
		}	
	}

	/*get ready to block sigint*/
	if(sigemptyset(&block_set)==-1){
		perror("sigemptyset");
		exit(EXIT_FAILURE);
	}
	if(sigaddset(&block_set, SIGINT)==-1){
		perror("sigaddset");
		exit(EXIT_FAILURE);
	}

	/*block sigint and fork off children 
 * 	for each command in the pipeline*/
	i=0;
	while(i < (plength)){
		if(sigprocmask(SIG_BLOCK, &block_set, NULL) == -1){
			perror("sigprocmask");
			exit(EXIT_FAILURE);
		}
		child = fork();
		
		/*child stuff*/
		if(child == 0){
			
			/*pipe redirection*/
			/*redirect stdin to read end of a pipe for 
 * 			everything except the first command*/
			if(i != 0){
				if(dup2(fds[2*(i-1)], STDIN_FILENO)==-1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
			}
			/*redirect stdout to a pipe's write end for 
 * 			everything but the last command*/
			if( i != (plength-1)){
				if(dup2(fds[(2*i)+1], STDOUT_FILENO)==-1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
			}

			/*redirect stdin/stdout if '>'/'<' 
 * 			were in the command line*/
			if(current->inname !=NULL){
				if((fdi =open(current->inname, O_RDONLY))== -1){
					perror("open");
					exit(EXIT_FAILURE);
				}
				if(dup2(fdi, STDIN_FILENO)==-1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
				close(fdi);
			}
			if( current->outname != NULL){
				fdo =open(current->outname, O_WRONLY | O_TRUNC 
| O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
				if(fdo==-1){
					perror("open");
					exit(EXIT_FAILURE);
				}
				if(dup2(fdo, STDOUT_FILENO)==-1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
				close(fdo);
			}
			
			/*clean up piping*/
			j=0;
			while(j < (plength-1)){
				close(fds[2*j]);
				close(fds[(2*j)+1]);
				j++;
			}
			free(fds);
			
			/*unblock sigint*/
			if(sigprocmask(SIG_UNBLOCK, &block_set, NULL)==-1){
				perror("sigprocmask");
				exit(EXIT_FAILURE);
			}
		
			/*exec the command and bail if it fails*/
			if(execvp(current->argv[0], current->argv) == -1){
				perror(current->argv[0]);
				_exit(EXIT_FAILURE);
			}
	
		}

		/*go to the next command*/
		commands->stage++;
		current = commands->stage;
		i++;
	}

	/*go back to the beginning of the pipeline 
 * so we can call free_pipeline*/
	commands->stage -= plength;

	/*unblock sigint for parent*/
	if(sigprocmask(SIG_UNBLOCK, &block_set, NULL)==-1){
		perror("sigprocmask");
		exit(EXIT_FAILURE);
	}

	/*clean up piping for parent*/
	i=0;
	while(i < (plength-1)){
		close(fds[2*i]);
		close(fds[(2*i)+1]);
		i++;
	}

	/*wait for all children to die*/
	while(plength > 0){
		/*if wait stopped because of sigint,
 * 		that child is alive and we have to wait again*/
		if(wait(&wstatus)==-1){
			if(errno == EINTR){
				continue;
			}else{
				perror("wait");
			}
		}
		plength--;
	}

	/*close  pipe file descriptors if they had to be allocated*/
	if(commands->length > 1){
		free(fds);
	}
}

/*changes directory, takes in the clstage that has cd's args*/
void cd(struct clstage *args){
	struct passwd *pass = NULL;

	/*if too many arguments we can't do cd*/
	if(args->argc > 2){
		fprintf(stderr, "usage: cd [ destdir ]\n");
	}

	/*if no args given go to home, 
 * if "HOME" isn't defined then look at password entry*/
	if(args->argc == 1){
		if(chdir(getenv("HOME"))==-1){
			pass = getpwuid(getuid());
			if( (pass==NULL) || chdir(pass->pw_dir) == -1){
				perror("cd");
			}
		}
	/*run chdir normally if an arg was given*/
	}else{
		if(chdir(args->argv[args->argc-1]) == -1){
			perror("cd");
		}
	}

}

/*sigint handler*/
/*doesn't do anything since the rest is handled by the shell*/
void handler(int signum){
}
