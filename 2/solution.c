#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>


int handle_exit_cmd(char *cmd, int* exit_status){
	if (strcmp(cmd, "exit") == 0)
	{
		*exit_status = 1;
		return 1;
	}

	return 0;
}

void execute_command(const struct expr *e, const struct command_line *line, int *exit_status, int *before_pipe, int *after_pipe, int* fd_prev, int* fd_next, int* p2p_count)
{
	char *argv[e->cmd.arg_count + 2];
	argv[0] = e->cmd.exe;
	for (uint32_t i = 0; i < e->cmd.arg_count; i++)
	{
		argv[i + 1] = e->cmd.args[i];
	}
	argv[e->cmd.arg_count + 2 - 1] = NULL;

	*exit_status = 0;
	int status = 0;
	if (strcmp(e->cmd.exe, "cd") == 0)
	{
		if (e->cmd.arg_count == 1)
		{
			int error = chdir(argv[1]);
			if (error != 0)
				printf("bash: %s: out: No such file or directory\n", e->cmd.exe);
		}
		else if (e->cmd.arg_count > 1)
			printf("bash: %s: too many arguments\n", e->cmd.exe);
	}
	else
	{
		if (*after_pipe == 0 && *before_pipe == 0)
		{
			if (handle_exit_cmd(e->cmd.exe, exit_status) != 0){ return; }
			int pid = fork();
			if (pid == 0)
			{
				if (line->out_type == OUTPUT_TYPE_STDOUT)
				{	
					int error = execvp(e->cmd.exe, argv);
					if (error != 0)
					{
						execlp("sh", "sh", "-c", "exit", NULL);
					}
				}
				else if (line->out_type == OUTPUT_TYPE_FILE_NEW)
				{
					int file = open(line->out_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
					dup2(file, STDOUT_FILENO);
					execvp(e->cmd.exe, argv);
				}
				else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
				{
					int file = open(line->out_file, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
					dup2(file, STDOUT_FILENO);
					execvp(e->cmd.exe, argv);
				}
			}
			else if (pid < 0)
			{
			}
			else if (pid > 0)
			{
				if (wait(&status) >= 0)
				{
					// printf("Child process exited with %d status\n", WEXITSTATUS(status));
				}
			}
		} else if (*after_pipe == 1 && *before_pipe == 0) {
			int pid = fork();
			if (pid == 0)
			{
				dup2(fd_prev[1], STDOUT_FILENO);
				close(fd_prev[1]);
				close(fd_prev[0]);
				close(fd_next[1]);
				close(fd_next[0]);
				int error = execvp(e->cmd.exe, argv);
				if (error != 0)
				{
					execlp("sh", "sh", "-c", "exit", NULL);
				}
			}
			else if (pid < 0)
			{
			}
			else if (pid > 0)
			{	 
				close(fd_prev[1]);

				if (wait(&status) >= 0)
				{
					// printf("Child process exited with %d status\n", WEXITSTATUS(status));
				}
			}

		} else if (*after_pipe == 1 && *before_pipe == 1) {

			int pid = fork();
			if (pid == 0)
			{
				if (*p2p_count % 2 == 0){
					dup2(fd_prev[0], STDIN_FILENO);
					dup2(fd_next[1], STDOUT_FILENO);
					close(fd_prev[1]);
					close(fd_prev[0]);
					close(fd_next[1]);
					close(fd_next[0]);
				} else {
					dup2(fd_next[0], STDIN_FILENO);
					dup2(fd_prev[1], STDOUT_FILENO);
					close(fd_prev[1]);
					close(fd_prev[0]);
					close(fd_next[1]);
					close(fd_next[0]);
				}
				int error = execvp(e->cmd.exe, argv);
				if (error != 0)
				{
					execlp("sh", "sh", "-c", "exit", NULL);
				}
			}
			else if (pid < 0)
			{
			}
			else if (pid > 0)
			{	 
				if (*p2p_count % 2 == 0){
					close(fd_next[1]);
					close(fd_prev[0]);
					pipe(fd_prev);
				} else {
					close(fd_prev[1]);
					close(fd_next[0]);
					pipe(fd_next);
				}

				if (wait(&status) >= 0)
				{
					*p2p_count += 1;
					// printf("Child process exited with %d status\n", WEXITSTATUS(status));
				}
			}	
		} else if (*after_pipe == 0 && *before_pipe == 1) {
			int pid = fork();
			if (pid == 0)
			{
				if (line->out_type == OUTPUT_TYPE_STDOUT)
				{	
					if (*p2p_count % 2 == 1){
						dup2(fd_next[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					} else {
						dup2(fd_prev[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					}
					int error = execvp(e->cmd.exe, argv);
					if (error != 0)
					{
						execlp("sh", "sh", "-c", "exit", NULL);
					}
				}
				else if (line->out_type == OUTPUT_TYPE_FILE_NEW)
				{
					if (*p2p_count % 2 == 1){
						dup2(fd_next[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					} else {
						dup2(fd_prev[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					}
					
					int file = open(line->out_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
					dup2(file, STDOUT_FILENO);
					close(file);
					execvp(e->cmd.exe, argv);
				}
				else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
				{
					if (*p2p_count % 2 == 1){
						dup2(fd_next[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					} else {
						dup2(fd_prev[0], STDIN_FILENO);
						close(fd_prev[0]);
						close(fd_prev[1]);
						close(fd_next[0]);
						close(fd_next[1]);
					}

					int file = open(line->out_file, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
					dup2(file, STDOUT_FILENO);
					close(file);
					execvp(e->cmd.exe, argv);
				}
			}
			else if (pid < 0)
			{
			}
			else if (pid > 0)
			{	
				close(fd_prev[0]);
				close(fd_prev[1]);
				close(fd_next[0]);
				close(fd_next[1]);

				if (wait(&status) >= 0)
				{
					// printf("Child process exited with %d status\n", WEXITSTATUS(status));
				}
			}
		}	
	}

	return;
}

static void
execute_command_line(const struct command_line *line, int *exit_status)
{
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

	assert(line != NULL);
	// printf("================================\n");
	// printf("Command line:\n");
	// printf("Is background: %d\n", (int)line->is_background);
	// printf("Output: ");
	if (line->out_type == OUTPUT_TYPE_STDOUT)
	{
		// printf("stdout\n");
	}
	else if (line->out_type == OUTPUT_TYPE_FILE_NEW)
	{
		// printf("new file - \"%s\"\n", line->out_file);
	}
	else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
	{
		// printf("append file - \"%s\"\n", line->out_file);
	}
	else
	{
		assert(false);
	}
	// printf("Expressions:\n");
	const struct expr *e = line->head;

	int before_pipe = 0, after_pipe = 0;
	int p2p_count = 0;
	int fd_prev[2], fd_next[2];
	pipe(fd_prev);
	pipe(fd_next);
	while (e != NULL)
	{
		if (e->type == EXPR_TYPE_COMMAND)
		{
			// printf("\tArgs: %d\n", e->cmd.arg_count);
			// printf("\tCommand: %s", e->cmd.exe);
			// for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
			// 	printf(" %s", e->cmd.args[i]);
			// printf("\n");
			if (e->next != NULL && e->next->type == EXPR_TYPE_PIPE)
			{
				after_pipe = 1;
			}
			else
			{
				after_pipe = 0;
			}
			execute_command(e, line, exit_status, &before_pipe, &after_pipe, fd_prev, fd_next, &p2p_count);
		}
		else if (e->type == EXPR_TYPE_PIPE)
		{
			before_pipe = 1;
			// printf("\tPIPE\n");
		}
		else if (e->type == EXPR_TYPE_AND)
		{
			// printf("\tAND\n");
		}
		else if (e->type == EXPR_TYPE_OR)
		{
			// printf("\tOR\n");
		}
		else
		{
			assert(false);
		}
		e = e->next;
	}
}

int main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	int exit_status = 0;
	struct parser *p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0)
	{
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true)
		{
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
			{
				break;
			}
			if (err != PARSER_ERR_NONE)
			{
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line, &exit_status);
			command_line_delete(line);
		}

		if (exit_status == 1)
		{
			break;
		}
	}
	parser_delete(p);
	return 0;
}
