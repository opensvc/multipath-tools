#define BINDINGS_FILE_HEADER \
"# Multipath bindings, Version : 1.0\n" \
"# NOTE: this file is automatically maintained by the multipath program.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Format:\n" \
"# alias wwid\n" \
"#\n"

int valid_alias(char *alias);
char *get_user_friendly_alias(char *wwid, char *file, char *prefix,
			      int bindings_readonly);
int get_user_friendly_wwid(char *alias, char *buff, char *file);
char *use_existing_alias (char *wwid, char *file, char *alias_old,
		char *prefix, int bindings_read_only);
