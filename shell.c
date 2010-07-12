#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>


#include "cache.h"
#include "quote.h"
#include "exec_cmd.h"
#include "strbuf.h"

#define COMMAND_DIR "git-shell-commands"
#define WAIT_FAILED 256

static int do_generic_cmd(const char *me, char *arg)
{
	const char *my_argv[4];

	setup_path();
	if (!arg || !(arg = sq_dequote(arg)))
		die("bad argument");
	if (prefixcmp(me, "git-"))
		die("bad command");

	my_argv[0] = me + 4;
	my_argv[1] = arg;
	my_argv[2] = NULL;

	return execv_git_cmd(my_argv);
}

static int do_cvs_cmd(const char *me, char *arg)
{
	const char *cvsserver_argv[3] = {
		"cvsserver", "server", NULL
	};

	if (!arg || strcmp(arg, "server"))
		die("git-cvsserver only handles server: %s", arg);

	setup_path();
	return execv_git_cmd(cvsserver_argv);
}

static int is_valid_cmd_name(char *cmd)
{
	/* TODO: come up with not-stupid validation */
	char *c;
	for (c = cmd; *c; c++) {
		if (*c == '.' || *c == '/')
			return 0;
	}
	return 1;
}

static int run(char *prog)
{
	int pid, res, w;
	/* TODO: check if prog is executable */
	if ( (pid = fork()) == 0 ) {
		execl(prog, prog, (char *) NULL);
		if (prog[0] != '\0')
			printf("unrecognized command '%s'\n", prog);
		exit(127);
	} else {
		do {
			res = waitpid (pid, &w, 0);
		} while (res <= 0 && errno == EINTR);

		if (res > 0) {
			if (WIFEXITED(w)) {
				return WEXITSTATUS(w);
			} else if (WIFSIGNALED(w)) {
				return -WTERMSIG(w);
			} else {
				/* TODO: can this happen? */
				return WAIT_FAILED;
			}
		} else {
			return WAIT_FAILED;
		}
	}
}


static struct commands {
	const char *name;
	int (*exec)(const char *me, char *arg);
} cmd_list[] = {
	{ "git-receive-pack", do_generic_cmd },
	{ "git-upload-pack", do_generic_cmd },
	{ "git-upload-archive", do_generic_cmd },
	{ "cvs", do_cvs_cmd },
	{ NULL },
};

int main(int argc, char **argv)
{
	char *prog;
	struct commands *cmd;
	int devnull_fd;

	/*
	 * Always open file descriptors 0/1/2 to avoid clobbering files
	 * in die().  It also avoids not messing up when the pipes are
	 * dup'ed onto stdin/stdout/stderr in the child processes we spawn.
	 */
	devnull_fd = open("/dev/null", O_RDWR);
	while (devnull_fd >= 0 && devnull_fd <= 2)
		devnull_fd = dup(devnull_fd);
	if (devnull_fd == -1)
		die_errno("opening /dev/null failed");
	close (devnull_fd);

	/*
	 * Special hack to pretend to be a CVS server
	 */
	if (argc == 2 && !strcmp(argv[1], "cvs server"))
		argv--;

	/*
	 * We do not accept anything but "-c" followed by "cmd arg",
	 * where "cmd" is a very limited subset of git commands.
	 */
	else if (argc != 3 || strcmp(argv[1], "-c")) {
		if (chdir(COMMAND_DIR))
			die("Sorry, the interactive git-shell is not enabled");
		for (;;) {
			prog = readline("git> ");
			if (prog == NULL) {
				printf("\n");
				exit(0);
			} else if (!strcmp(prog, "quit") || !strcmp(prog, "logout") ||
				   !strcmp(prog, "exit")) {
				exit(0);
			} else if (!strcmp(prog, "")) {
			} else if (is_valid_cmd_name(prog)) {
				run(prog);
				add_history(prog);
			} else {
				printf("invalid command format '%s'\n", prog);
			}
			free(prog);
		};
	}

	prog = argv[2];
	if (!strncmp(prog, "git", 3) && isspace(prog[3]))
		/* Accept "git foo" as if the caller said "git-foo". */
		prog[3] = '-';

	for (cmd = cmd_list ; cmd->name ; cmd++) {
		int len = strlen(cmd->name);
		char *arg;
		if (strncmp(cmd->name, prog, len))
			continue;
		arg = NULL;
		switch (prog[len]) {
		case '\0':
			arg = NULL;
			break;
		case ' ':
			arg = prog + len + 1;
			break;
		default:
			continue;
		}
		exit(cmd->exec(cmd->name, arg));
	}

	if (chdir(COMMAND_DIR))
		die("unrecognized command '%s'", prog);

	if (is_valid_cmd_name(prog))
		execl(prog, prog, (char *) NULL);

	die("unrecognized command '%s'", prog);
}
