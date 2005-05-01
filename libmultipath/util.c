#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PARAMS_SIZE 255

int
strcmp_chomp(char *str1, char *str2)
{
	int i;
	char s1[PARAMS_SIZE],s2[PARAMS_SIZE];
 
	if(!str1 || !str2)
		return 1;

	strncpy(s1, str1, PARAMS_SIZE);
	strncpy(s2, str2, PARAMS_SIZE);

	for (i=strlen(s1)-1; i >=0 && isspace(s1[i]); --i) ;
	s1[++i] = '\0';
	for (i=strlen(s2)-1; i >=0 && isspace(s2[i]); --i) ;
	s2[++i] = '\0';

	return(strcmp(s1,s2));
}

void
basename (char * str1, char * str2)
{
	char *p = str1 + (strlen(str1) - 1);

	while (*--p != '/' && p != str1)
		continue;

	if (p != str1)
		p++;

	strcpy(str2, p);
}

int
filepresent (char * run) {
	struct stat buf;

	if(!stat(run, &buf))
		return 1;
	return 0;
}

