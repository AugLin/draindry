/* Name: Caifu Lin
   UID: 116447591 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>

#include "command.h"
#include "executor.h"


#define FILE_PERMISSIONS 0664

/* static void print_tree(struct tree *t); */
int execute_aux(struct tree *t, int p_input_fd, int p_output_fd);

int execute(struct tree *t) {

   return execute_aux(t, STDIN_FILENO, STDOUT_FILENO);
}

/*static void print_tree(struct tree *t) {
   if (t != NULL) {
      print_tree(t->left);

      if (t->conjunction == NONE) {
         printf("NONE: %s, ", t->argv[0]);
      } else {
         printf("%s, ", conj[t->conjunction]);
      }
      printf("IR: %s, ", t->input);
      printf("OR: %s\n", t->output);

      print_tree(t->right);
   }
} */

int execute_aux(struct tree *t, int p_input_fd, int p_output_fd) {
   char* cd_dir;
   pid_t process_id;
   int status, pipe_fd[2], std_out;

   if (t == NULL) {
      return 0;
   }

   if (t->conjunction == NONE) {
      if (!strcmp(t->argv[0], "cd")) {
         if (t->argv[1]) {
            cd_dir = t->argv[1];
         } else {
            cd_dir = getenv("HOME");   
         }
         if (chdir(cd_dir)) {
            perror(cd_dir);
         }
      } else if (!strcmp(t->argv[0], "exit")) {
         exit(0);
      } else {
         if ((process_id = fork()) < 0) {
            perror("fork error");
         } else {
            /* parent process */
            if (process_id) {
               wait(&status);
               if (WEXITSTATUS(status)) {
                  return -1;
               } else {
                  return 0;
               }
            } else {
               /* child process */
               /* determine whether need to change input/output */
               if (t->input) {
                  if ((p_input_fd = open(t->input, O_RDONLY)) < 0) {
                     perror("open input error");
                  }
                  if (dup2(p_input_fd, STDIN_FILENO) < 0) { 
                     perror("dup2 input error");
                  }
                  /* prevent resource leak */
                  if (close(p_input_fd) < 0) { 
                     perror("close input error");
                  }
               }
               if (t->output) {
                  if ((p_output_fd = open(t->output,
                     O_WRONLY | O_CREAT | O_TRUNC,
                     FILE_PERMISSIONS)) < 0) {
                     perror("open output error");
                  }
                  if (dup2(p_output_fd, STDOUT_FILENO) < 0) { 
                     perror("dup2 output error");
                  }
                  /* prevent resource leak */
                  if (close(p_output_fd) < 0) { 
                     perror("close output error");
                  }
               }
               execvp(t->argv[0], (char* const*) t->argv);
               fprintf(stderr, "Failed to execute %s\n", t->argv[0]);
               exit(-1);
            }
         }
      }
   } else if (t->conjunction == AND) {
      status = execute_aux(t->left, p_input_fd, p_output_fd);
      if (!status) {
         status = execute_aux(t->right, p_input_fd, p_output_fd);
      }
      return status;
   } else if (t->conjunction == SUBSHELL) {
      if ((process_id = fork()) < 0) {
         perror("fork error");
      } else {
         /* parent process */
         if (process_id) {
            /* wait for child to die */
            wait(&status);
         } else {
            if (t->input) {
               if ((p_input_fd = open(t->input, O_RDONLY)) < 0) {
                  perror("open input error");
               }
               /* redirect input */
               if (dup2(p_input_fd, STDIN_FILENO) < 0) { 
                  perror("dup2 input error");
               }
               /* prevent resource leak */
               if (close(p_input_fd) < 0) { 
                  perror("close input error");
               }
            }
            if (t->output) {
               if ((p_output_fd = open(t->output,
                  O_WRONLY | O_CREAT | O_TRUNC, 
                  FILE_PERMISSIONS)) < 0) {
                  perror("open output error");
               }
               /* redirect output */
               if (dup2(p_output_fd, STDOUT_FILENO) < 0) { 
                  perror("dup2 output error");
               }
               /* prevent resource leak */
               if (close(p_output_fd) < 0) { 
                  perror("close output error");
               }
            }
            /* proceed with the left subtree in the child process */
            execute_aux(t->left, p_input_fd, p_output_fd);
            /* exit child process */
            exit(0);
         }
      }
      
   } else if (t->conjunction == PIPE) {
      if (t->left->output) {
         fprintf(stdout, "Ambiguous output redirect.\n");
      } else if (t->right->input) {
         fprintf(stdout, "Ambiguous input redirect.\n");
      } else {
         /* create pipe */
         if (pipe(pipe_fd) < 0) {
            perror("pipe error");
         }
         /* create child process */
         if ((process_id = fork()) < 0) {
            perror("fork error");
         }
         /* parent process left subtree */
         if (process_id) {
            if (t->input) {
               if ((p_input_fd = open(t->input, O_RDONLY)) < 0) {
                  perror("open input error");
               }
               if (dup2(p_input_fd, STDIN_FILENO) < 0) { 
                  perror("dup2 input error");
               }
               /* prevent resource leak */
               if (close(p_input_fd) < 0) { 
                  perror("close input error");
               }
            }
            /* close read end */
            if (close(pipe_fd[0]) < 0) { 
               perror("close pipe input error");
            }
            std_out = dup(STDOUT_FILENO);
            /* redirect output */
            if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) { 
               perror("dup2 pipe output error");
            }
            /* process left subtree, output is redirected */
            execute_aux(t->left, p_input_fd, pipe_fd[1]);
            
            dup2(std_out, STDOUT_FILENO);
            if (close(std_out) < 0) { 
               perror("close pipe output error");
            }
            /* close write end */
            if (close(pipe_fd[1]) < 0) { 
               perror("close pipe output error");
            }
            /* wait for child to die */
            wait(&status);
         /* child process right subtree */
         } else {
            if (t->output) {
               if ((p_output_fd = open(t->output,
                  O_WRONLY | O_CREAT | O_TRUNC, 
                  FILE_PERMISSIONS)) < 0) {
                  perror("open output error");
               }
               if (dup2(p_output_fd, STDOUT_FILENO) < 0) { 
                  perror("dup2 output error");
               }
               /* prevent resource leak */
               if (close(p_output_fd) < 0) { 
                  perror("close output error");
               }
            }
            /* close write end */
            if (close(pipe_fd[1]) < 0) { 
               perror("close pipe output error");
            }
            /* redirect input */
            if (dup2(pipe_fd[0], STDIN_FILENO) < 0) { 
               perror("dup2 pipe input error");
            }
            /* process right subtree, input is redirected */
            execute_aux(t->right, pipe_fd[0], p_output_fd);
            /* close read end */
            if (close(pipe_fd[0]) < 0) { 
               perror("close pipe input error");
            }
            /* exit child process */
            exit(0);
         }
      }
   }

   return 0;
}