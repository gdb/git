#include <dlfcn.h>

#include "cache.h"
#include "quote.h"
#include "run-command.h"

#define COMMAND_DIR "git-shell-commands"
#define HELP_COMMAND "git-shell-commands/help"

struct readline_data {
	void *handle;
	char *(*readline)(const char *);
	void (*add_history)(const char *);
};
static struct readline_data rl_data = {};

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

static char *git_shell_command_generator(const char *text, int state)
{
	static const char *help_cmd[4] = { HELP_COMMAND, "--prefix", NULL, NULL };
	static struct child_process help_proc = {.argv = help_cmd, .no_stdin = 1,
						 .no_stderr = 1, .silent_exec_failure = 1};
	static int running = 0;
	static FILE *cmd_list;
	struct strbuf line = {};

	/* Initialize */
	if (!state)
	{
		if (running) {
			fclose(cmd_list);
			finish_command(&help_proc);
		}
		help_proc.out = -1;
		help_cmd[2] = text;
		if (start_command(&help_proc) != -1) {
			if ((cmd_list = fdopen(help_proc.out, "r")) == NULL)
				goto finish_cmd;
			running = 1;
		} else {
			goto abort;
		}
	}

	assert(running);
	if (strbuf_getline(&line, cmd_list, '\n') != EOF) {
		return strbuf_detach(&line, NULL);
	}

	strbuf_release(&line);
	fclose(cmd_list);
finish_cmd:
	finish_command(&help_proc);
abort:
	running = 0;
	return NULL;
}

static void *xdlsym(void *handle, const char *name)
{
	void *sym;
	char *err;
	dlerror();
	sym = dlsym(handle, name);
	if (sym == NULL && (err = dlerror()))
		die("error finding symbol %s: %s", name, err);
	return sym;
}

static void initialize_readline(void)
{
	char **rl_readline_name;
	char *(**rl_completion_entry_function)(const char *, int);

	if ((rl_data.handle = dlopen("libreadline.so", RTLD_NOW)) == NULL) {
		rl_data = (struct readline_data) {};
		return;
	}
	rl_readline_name = xdlsym(rl_data.handle, "rl_readline_name");
	rl_completion_entry_function = xdlsym(rl_data.handle, "rl_completion_entry_function");
	rl_data.readline = xdlsym(rl_data.handle, "readline");
	rl_data.add_history = xdlsym(rl_data.handle, "add_history");

	*rl_readline_name = "git-shell";
	*rl_completion_entry_function = git_shell_command_generator;
}

static void shutdown_readline(void)
{
	if (rl_data.handle)
		dlclose(rl_data.handle);
}

static void run_shell(void)
{
	struct strbuf line = STRBUF_INIT;
	const char *prog;
	char *full_cmd;
	char *rawargs;
	const char **argv;
	char *rawargs_cpy;
	int code;
	int done = 0;
	int add_to_history = 0;

	/* Print help if enabled */
	argv = xmalloc(3 * sizeof(char *));
	argv[0] = HELP_COMMAND;
	argv[1] = "--login";
	argv[2] = NULL;
	code = run_command_v_opt(argv, RUN_SILENT_EXEC_FAILURE);
	free(argv);

	initialize_readline();
	do {
		if (rl_data.readline) {
			if ((rawargs = rl_data.readline("git> ")) == NULL) {
				fprintf(stderr, "\n");
				goto shutdown;
			}
			strbuf_attach(&line, rawargs, strlen(rawargs), strlen(rawargs) + 1);
		} else {
			fprintf(stderr, "git> ");
			if (strbuf_getline(&line, stdin, '\n') == EOF) {
				fprintf(stderr, "\n");
				strbuf_release(&line);
				goto shutdown;
			}
		}
		strbuf_trim(&line);
		rawargs = strbuf_detach(&line, NULL);
		rawargs_cpy = xstrdup(rawargs);
		if (split_cmdline(rawargs, &argv) == -1) {
			free(rawargs);
			continue;
		}

		prog = argv[0];
		if (!strcmp(prog, "")) {
			add_to_history = 0;
		} else if (!strcmp(prog, "quit") || !strcmp(prog, "logout") ||
			   !strcmp(prog, "exit") || !strcmp(prog, "bye")) {
			done = 1;
			add_to_history = 0;
		} else if (is_valid_cmd_name(argv[0])) {
			full_cmd = make_cmd(argv[0]);
			argv[0] = full_cmd;
			code = run_command_v_opt(argv, RUN_SILENT_EXEC_FAILURE);
			if (code == -1 && errno == ENOENT) {
				fprintf(stderr, "unrecognized command '%s'\n", prog);
			}
			free(full_cmd);
			add_to_history = 1;
		} else {
			fprintf(stderr, "invalid command format '%s'\n", prog);
			add_to_history = 1;
		}

		if (add_to_history && rl_data.add_history)
			rl_data.add_history(rawargs_cpy);
		free(argv);
		free(rawargs);
	} while (!done);

shutdown:
	shutdown_readline();
	exit(0);
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
