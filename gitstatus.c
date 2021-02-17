#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

#define MAX_PATH_LENGTH 1024
#define MAX_NAME_LENGTH 128

int is_directory(char *path)
{
    struct stat path_stat;

    stat(path, &path_stat);

    return S_ISDIR(path_stat.st_mode);
}

int is_file(char *path)
{
    struct stat path_stat;

    stat(path, &path_stat);

    return S_ISREG(path_stat.st_mode);
}

void rebase_progress(char *rebase_dir, char *rebase, int len)
{
    int next = 0, last = 0;
    char next_file_path[MAX_PATH_LENGTH];
    char last_file_path[MAX_PATH_LENGTH];
    FILE *fp;

    snprintf(next_file_path, sizeof(next_file_path), "%s/next", rebase_dir);
    snprintf(last_file_path, sizeof(last_file_path), "%s/last", rebase_dir);

    if (!is_file(next_file_path) || !is_file(last_file_path))
        return;

    if ((fp = fopen(next_file_path, "r")) != NULL) {
        fscanf(fp, "%d", &next);
        fclose(fp);
    }

    if ((fp = fopen(last_file_path, "r")) != NULL) {
        fscanf(fp, "%d", &last);
        fclose(fp);
    }

    snprintf(rebase, len, "%d/%d", next, last);
}

int stash_count(char *stash_file)
{
    FILE *fp;
    int ch;
    int stashes = 0;

    if ((fp = fopen(stash_file, "r")) != NULL) {
        while ((ch = fgetc(fp)) != EOF) {
            if (ch == '\n') {
                stashes++;
            }
        }

        fclose(fp);
    }

    return stashes;
}

int parse_ahead_behind(char *branch, char *what)
{
    char *pos;
    int value = 0;

    pos = strstr(branch, what);

    if (pos != NULL) {
        for (pos += strlen(what); isdigit(*pos); pos++) {
            value = value * 10 + *pos - '0';
        }
    }

    return value;
}

void parse_branch(char *line, char *head_file, char *branch, char *upstream, int *local)
{
    int i, j;
    FILE *fp;

    if (strstr(line, "no branch") != NULL) {
        *local = 0;

        if ((fp = fopen(head_file, "r")) != NULL) {
            branch[0] = ':';
            fgets(branch + 1, 8, fp);
            fclose(fp);
        }
    } else if (strstr(line, "Initial commit") != NULL || strstr(line, "No commits yet") != NULL) {
        for (i = strlen(line); line[i] != ' ' && i >= 0; i--)
            ;
        strcpy(branch, line + i + 1);
    } else {
        for (i = 3; line[i] != '.' && line[i] != '\0'; i++)
            branch[i - 3] = line[i];
        branch[i - 3] = '\0';

        if (strstr(line, "...") != NULL) {
            *local = 0;

            for (j = i + 3; line[j] != '\0' && line[j] != '\n' && line[j] != ' ' && line[j] != '['; j++)
                upstream[j - i - 3] = line[j];
            upstream[j - i - 3] = '\0';
        }
    }
}

void parse_stat_line(char *line, int *staged, int *conflicts, int *changed, int *untracked)
{
    if (line[0] == '?' && line[1] == '?')
        ++*untracked;

    if ((line[0] == 'A' && (line[1] == 'A' || line[1] == 'D')) ||
        (line[0] == 'D' && (line[1] == 'D' || line[1] == 'U')) ||
        (line[0] == 'U' && (line[1] == 'A' || line[1] == 'D' || line[1] == 'U')))
        ++*conflicts;

    if (line[0] == 'A' || line[0] == 'C' || line[0] == 'D' || line[0] == 'M' || line[0] == 'R')
        ++*staged;
    if (line[1] == 'C' || line[1] == 'D' || line[1] == 'M' || line[1] == 'R')
        ++*changed;
}

int find_git_root(char *path, int len)
{
    char *cwd, *p;
    char buff[1024];
    FILE *fp;

    cwd = getcwd(NULL, 0);

    while (strcmp(cwd, "/")) {
        snprintf(path, len, "%s/.git", cwd);

        if (is_directory(path))
            return 1;

        if (is_file(path)) {
            if ((fp = fopen(path, "r")) == NULL) {
                perror("fopen");
                return 0;
            }

            if (fgets(buff, sizeof buff, fp) == NULL) {
                perror("fgets");
                return 0;
            }

            p = buff;

            while (*p++ != ':'); /* find next to : */
            while (*++p == ' '); /* strip spaces */

            while (*p != ' ' && *p != '\n' && *p != '\r' && *p != '\0')
                *path++ = *p++;

            *path = '\0';

            fclose(fp);

            return 1;
        }

        cwd = dirname(cwd);
    }

    return 0;
}

int main()
{
    FILE *in;
    char line[MAX_PATH_LENGTH * 2];
    char branch[MAX_NAME_LENGTH];
    char upstream[MAX_NAME_LENGTH] = "..";
    char rebase[MAX_NAME_LENGTH] = "0";
    char git_root[MAX_PATH_LENGTH];
    char stash_file[MAX_PATH_LENGTH];
    char head_file[MAX_PATH_LENGTH];
    char merge_file[MAX_PATH_LENGTH];
    char rebase_dir[MAX_PATH_LENGTH];
    int stashes = 0;
    int local = 1;
    int ahead = 0;
    int behind = 0;
    int merge = 0;
    int staged = 0;
    int conflicts = 0;
    int changed = 0;
    int untracked = 0;

    if (!isatty(0))
        in = stdin;
    else
        in = popen("git status --branch --porcelain 2>&1", "r");

    fgets(line, sizeof(line), in);
    line[strlen(line) - 1] = '\0'; /* remove newline */

    if (strstr(line, "fatal: not a git repository") != NULL) {
        fprintf(stderr, "Not a git repository\n");
        exit(EXIT_SUCCESS);
    }

    if (!find_git_root(git_root, sizeof(git_root))) {
        fprintf(stderr, "Cannot find git root\n");
        exit(EXIT_SUCCESS);
    }

    snprintf(head_file, sizeof(head_file), "%s/HEAD", git_root);
    snprintf(stash_file, sizeof(stash_file), "%s/logs/refs/stash", git_root);
    snprintf(merge_file, sizeof(merge_file), "%s/MERGE_HEAD", git_root);
    snprintf(rebase_dir, sizeof(rebase_dir), "%s/rebase-apply", git_root);

    parse_branch(line, head_file, branch, upstream, &local);
    ahead = parse_ahead_behind(line, "ahead ");
    behind = parse_ahead_behind(line, "behind ");
    stashes = stash_count(stash_file);
    merge = is_file(merge_file);
    rebase_progress(rebase_dir, rebase, sizeof(rebase));

    while (fgets(line, sizeof(line), in) != NULL) {
        parse_stat_line(line, &staged, &conflicts, &changed, &untracked);
    }

    printf("%s %d %d %d %d %d %d %d %d %s %d %s", branch, ahead, behind, staged, conflicts, changed, untracked, stashes,
           local, upstream, merge, rebase);

    return EXIT_SUCCESS;
}
