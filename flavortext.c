#include <stdio.h>
#include <cache.h>
#include <flavortext.h>

struct flavortext_list_entry {
	const char *key;
	const char *val;
	struct flavortext_list_entry *next;
};

struct flavortext_list {
	struct flavortext_list_entry *first;
	struct flavortext_list_entry *last;
};

static struct flavortext_list flist;
static int initialized = 0;

/* Copied from alias.c */
static const char *flavortext_key;
static const char *flavortext_val;

static int flavortext_lookup_cb(const char *k, const char *v, void *cb)
{
	if (!prefixcmp(k, "flavortext.") && !strcmp(k+strlen("flavortext."), flavortext_key)) {
		if (!v)
			return config_error_nonbool(k);
		flavortext_val = xstrdup(v);
		return 0;
	}
	return 0;
}

static const char *config_lookup(const char *key)
{
	flavortext_key = key;
	flavortext_val = NULL;
	git_config(flavortext_lookup_cb, NULL);
	return flavortext_val;
}

void flavortext_init() {
	if (initialized)
		return;
	initialized = 1;
	/* builtins/help.c */
	flavortext_insert("commonly-used-commands", "The most commonly used git commands are:");

	/* transport.c */
	flavortext_insert("no-match", "[no match]");
	flavortext_insert("rejected", "[rejected]");
	flavortext_insert("up-to-date", "[up to date]");
	flavortext_insert("remote-rejected", "[remote rejected]");
	flavortext_insert("remote-failure", "[remote failure]");
}

void flavortext_insert(const char *key, const char *val)
{
	struct flavortext_list_entry *le;
	for (le = flist.first; le != NULL; le = le->next) {
		if (strcmp(le->key, key) == 0) {
			le->val = xstrdup(val);
			return;
		}
	}
	le = xmalloc(sizeof(struct flavortext_list_entry));
	le->key = xstrdup(key);
	le->val = xstrdup(val);
	le->next = NULL;
	/* Nothing at beginning, so make this the first */
	if (flist.first == NULL)
		flist.first = le;
	/* Something at the end, so give a next pointer */
	if (flist.last != NULL)
		flist.last->next = le;
	/* Insert at end */
	flist.last = le;
}

const char *flavortext_lookup(const char *key)
{
	struct flavortext_list_entry *le;
	config_lookup(key);
	if (flavortext_val != NULL)
		return flavortext_val;
	for (le = flist.first; le != NULL; le = le->next) {
                if (strcmp(le->key, key) == 0) {
                        return le->val;
                }
        }
	die("Could not find flavortext key: %s", key);
	return NULL;
}
