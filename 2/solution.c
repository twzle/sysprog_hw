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

void initialize_pipes(int *pipes_count, int **fd)
{
	for (int i = 0; i < *pipes_count; i++)
	{
		fd[i] = (int *)&fd[*pipes_count] + i * 2;
		pipe(fd[i]);
	}
}

void count_pipes(const struct expr *e, int *pipes_count)
{
	while (e != NULL)
	{
		if (e->type == EXPR_TYPE_PIPE)
		{
			*pipes_count += 1;
		}
		e = e->next;
	}
}

int handle_exit_cmd(char *cmd, int *exit_status)
{
	if (strcmp(cmd, "exit") == 0)
	{
		*exit_status = 1;
		return 1;
	}

	return 0;
}

void free_cmd_atributes(int** fd, int* child_pid){
	free(fd);
	free(child_pid);
}

int execute_command(const struct expr *e, const struct command_line *line, int **fd, int *child_pid, int pipes_count, int cmd_id)
{
	char *argv[e->cmd.arg_count + 2];
	argv[0] = e->cmd.exe;
	for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
	{
		argv[i + 1] = e->cmd.args[i];
	}
	argv[e->cmd.arg_count + 2 - 1] = NULL;

	int exit_flag = 0;

	if (strcmp(e->cmd.exe, "cd") == 0)
	{
		if (pipes_count == 0)
		{	
			int error = chdir(argv[1]);
			if (error != 0)
				printf("bash: %s: out: No such file or directory\n", e->cmd.exe);
		}

		return 0;
	}

	if (strcmp(e->cmd.exe, "exit") == 0)
	{
		if (pipes_count == 0)
		{	
			return 1;
		}

		exit_flag = 1;
	}

	if (strcmp(e->cmd.exe, "#") == 0){
		return 2;
	}

	int pid = fork();
	if (pid == 0)
	{
		if (cmd_id > 0)
		{
			int new_fd = dup2(fd[cmd_id - 1][0], STDIN_FILENO);
			if (new_fd == -1)
			{
				perror("dup2");
				exit(EXIT_FAILURE);
			}
		}

		if (cmd_id < pipes_count)
		{	
			int new_fd = dup2(fd[cmd_id][1], STDOUT_FILENO);
			if (new_fd == -1)
			{
				perror("dup2");
				exit(EXIT_FAILURE);
			}
		}

		if (cmd_id == pipes_count) {
			int new_fd;
			if (line->out_type == OUTPUT_TYPE_FILE_NEW){
				int file = open(line->out_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
				new_fd = dup2(file, STDOUT_FILENO);
				close(file);
			} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND){
				int file = open(line->out_file, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
				new_fd = dup2(file, STDOUT_FILENO);
				close(file);
			}

			if (new_fd == -1)
			{
				perror("dup2");
				exit(EXIT_FAILURE);
			}

		}

		for (int j = 0; j < pipes_count; j++)
		{
			close(fd[j][0]);
			close(fd[j][1]);
		}

		if (exit_flag == 1){
			execlp("sh", "sh", "-c", "exit", NULL);
		}

		int error = execvp(e->cmd.exe, argv);
		if (error != 0)
		{
			// fprintf(stderr, "exec %d", error);
			exit(error);
		}
	}
	else if (pid > 0)
	{
		child_pid[cmd_id] = pid;
	}
	else if (pid == -1)
	{
	}

	return 0;
}

static void
execute_command_line(const struct command_line *line, int* exit_status)
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
	int pipes_count = 0;
	int cmd_id = 0;
	*exit_status = 0;

	count_pipes(line->head, &pipes_count);

	int **fd = malloc(pipes_count * sizeof(int *) + pipes_count * sizeof(int) * 2);
	int *child_pid = malloc((pipes_count + 1) * sizeof(int));
	initialize_pipes(&pipes_count, fd);

	while (e != NULL)
	{
		if (e->type == EXPR_TYPE_COMMAND)
		{
			// printf("\tArgs: %d\n", e->cmd.arg_count);
			// printf("\tCommand: %s", e->cmd.exe);
			// for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
			// 	printf(" %s", e->cmd.args[i]);
			// printf("\n");

			*exit_status = execute_command(e, line, fd, child_pid, pipes_count, cmd_id);
			if (*exit_status == 1){
				break;
			} else if (*exit_status == 2){
				cmd_id--;
			}
			cmd_id++;
		}
		else if (e->type == EXPR_TYPE_PIPE)
		{
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

	for (int i = 0; i < pipes_count; i++)
	{
		close(fd[i][0]);
		close(fd[i][1]);
	}

	int wstatus;
	for (int i = 0; i < pipes_count + 1; i++)
	{
		waitpid(child_pid[i], &wstatus, 0);
		if (WIFEXITED(wstatus))
		{
			// printf("parent: child_%d exited normally with status %d\n", i, WEXITSTATUS(wstatus));
		}
		if (WIFSIGNALED(wstatus))
		{
			// printf("parent: child_%d was terminated by signal %d\n", i, WTERMSIG(wstatus));
		}
	}

	free_cmd_atributes(fd, child_pid);
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
