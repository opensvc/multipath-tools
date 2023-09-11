#ifndef _ALIAS_H
#define _ALIAS_H

int valid_alias(const char *alias);
int get_user_friendly_wwid(const char *alias, char *buff);
char *get_user_friendly_alias(const char *wwid, const char *alias_old,
			      const char *prefix, bool bindings_read_only);

struct config;
int check_alias_settings(const struct config *);
void cleanup_bindings(void);
struct inotify_event;
void handle_bindings_file_inotify(const struct inotify_event *event);
#endif /* _ALIAS_H */
