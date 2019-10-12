#define BINDINGS_FILE_HEADER \
"# Multipath bindings, Version : 1.0\n" \
"# NOTE: this file is automatically maintained by the multipath program.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Format:\n" \
"# alias wwid\n" \
"#\n"

int valid_alias(const char *alias);
char *get_user_friendly_alias(const char *wwid, const char *file,
			      const char *prefix,
			      int bindings_readonly);
int get_user_friendly_wwid(const char *alias, char *buff, const char *file);
char *use_existing_alias (const char *wwid, const char *file,
			  const char *alias_old,
			  const char *prefix, int bindings_read_only);
