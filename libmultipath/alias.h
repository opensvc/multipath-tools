#define BINDINGS_FILE_TIMEOUT 3
#define BINDINGS_FILE_HEADER \
"# Multipath bindings, Version : 1.0\n" \
"# NOTE: this file is automatically maintained by the multipath program.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Format:\n" \
"# alias wwid\n" \
"#\n"

char *get_user_friendly_alias(char *wwid, char *file);
char *get_user_friendly_wwid(char *alias, char *file);
