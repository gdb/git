#include "cache.h"
#include "quote.h"
#include "run-command.h"

#define COMMAND_DIR "git-shell-commands"
#define HELP_COMMAND "git-shell-commands/help"

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

static int is_valid_cmd_name(const char *cmd)
{
	/* Test command contains no . or / characters */
	return cmd[strcspn(cmd, "./")] == '\0';
}

static char *make_cmd(const char *prog)
{
	char *prefix = xmalloc((strlen(prog) + strlen(COMMAND_DIR) + 2) * sizeof(char));
	strcpy(prefix, COMMAND_DIR);
	strcat(prefix, "/");
	strcat(prefix, prog);
	return prefix;
}

static void run_shell(void)
{
	struct strbuf line = STRBUF_INIT;
	const char *prog;
	char *full_cmd;
	char *rawargs;
	const char **argv;
	int code;
	int done = 0;

	/* Print help if enabled */
	argv = xmalloc(2 * sizeof(char *));
	argv[0] = HELP_COMMAND;
	argv[1] = NULL;
	code = run_command_v_opt(argv, RUN_SILENT_EXEC_FAILURE);
	free(argv);

	do {
		fprintf(stderr, "git> ");
		if (strbuf_getline(&line, stdin, '\n') == EOF) {
			fprintf(stderr, "\n");
			strbuf_release(&line);
			exit(0);
		}
		strbuf_trim(&line);
		rawargs = strbuf_detach(&line, NULL);
		if (split_cmdline(rawargs, &argv) == -1) {
			free(rawargs);
			continue;
		}

		prog = argv[0];
		if (!strcmp(prog, "")) {
		} else if (!strcmp(prog, "quit") || !strcmp(prog, "logout") ||
			   !strcmp(prog, "exit") || !strcmp(prog, "bye")) {
			done = 1;
		} else if (is_valid_cmd_name(argv[0])) {
			full_cmd = make_cmd(argv[0]);
			argv[0] = full_cmd;
			code = run_command_v_opt(argv, RUN_SILENT_EXEC_FAILURE);
			if (code == -1 && errno == ENOENT) {
				fprintf(stderr, "unrecognized command '%s'\n", prog);
			}
			free(full_cmd);
		} else {
			fprintf(stderr, "invalid command format '%s'\n", prog);
		}

		free(argv);
		free(rawargs);
	} while (!done);
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
	char *prog_cpy;
	const char **user_argv;
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
	if (argc == 2 && !strcmp(argv[1], "cvs server")) {
		argv--;
	}
	/*
	 * Allow the user to run an interactive shell
	 */
	else if (argc == 1) {
		if (access(COMMAND_DIR, R_OK | X_OK) == -1)
			die("Sorry, the interactive git-shell is not enabled");
		run_shell();
		exit(0);
	}
	/*
	 * We do not accept any other modes except "-c" followed by
	 * "cmd arg", where "cmd" is a very limited subset of git
	 * commands or a command in the COMMAND_DIR
	 */
	else if (argc != 3 || strcmp(argv[1], "-c")) {
		die("Run with no arguments or with -c cmd");
	}

	prog = argv[2];
	prog_cpy = xstrdup(prog);
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

	if (split_cmdline(prog, &user_argv) != -1) {
		if (is_valid_cmd_name(user_argv[0])) {
			prog  = make_cmd(user_argv[0]);
			user_argv[0] = prog;
			execv(user_argv[0], (char *const *) user_argv);
			free(prog);
		}
		free(user_argv);
		/*
		 * split_cmdline modifies its argument in-place, so 'prog' now
		 * holds the actual command name
		 */
		die("unrecognized command '%s'", prog_cpy);
	} else {
		/*
		 * split_cmdline has clobbered prog and printed an
		 * error message, so print the original
		 */
		die("invalid command format '%s'", prog_cpy);
	}
}
