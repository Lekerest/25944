#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <ulimit.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

extern char **environ;


/*
 * Структура для хранения одной опции командной строки.
 * code хранит символ опции, например 'i' или 'U'.
 * arg хранит аргумент опции, если он требуется.
 * next указывает на следующую опцию в списке.
 */
typedef struct Option
{
    int code;
    char *arg;
    struct Option *next;
} Option;


/*
 * Преобразует строку в неотрицательное число.
 * Используется для параметров опций -U и -C.
 *
 * Возвращает:
 *  0  - строка успешно преобразована;
 * -1  - значение некорректно.
 */
static int parse_number(const char *str, long *value)
{
    char *end;

    errno = 0;
    *value = strtol(str, &end, 10);

    if (errno != 0 || end == str || *end != '\0' || *value < 0)
    {
        return -1;
    }

    return 0;
}


/*
 * Обрабатывает опцию -i.
 * Печатает реальные и эффективные идентификаторы
 * пользователя и группы текущего процесса.
 */
static void print_ids(void)
{
    printf("Real UID: %ld\n", (long)getuid());
    printf("Effective UID: %ld\n", (long)geteuid());
    printf("Real GID: %ld\n", (long)getgid());
    printf("Effective GID: %ld\n", (long)getegid());
}


/*
 * Обрабатывает опцию -s.
 * Делает текущий процесс лидером своей группы процессов.
 */
static int make_group_leader(void)
{
    if (setpgid(0, 0) == -1)
    {
        perror("setpgid");
        return -1;
    }

    return 0;
}


/*
 * Обрабатывает опцию -p.
 * Печатает идентификатор текущего процесса,
 * его родителя и группы процессов.
 */
static void print_process_ids(void)
{
    printf("PID: %ld\n", (long)getpid());
    printf("PPID: %ld\n", (long)getppid());
    printf("PGID: %ld\n", (long)getpgrp());
}


/*
 * Обрабатывает опцию -u.
 * Получает и печатает текущее значение ulimit.
 */
static int print_ulimit(void)
{
    long value;

    errno = 0;
    value = ulimit(UL_GETFSIZE, 0);

    if (value == -1 && errno != 0)
    {
        perror("ulimit");
        return -1;
    }

    printf("%ld\n", value);

    return 0;
}


/*
 * Обрабатывает опцию -U.
 * Преобразует переданный аргумент в число
 * и устанавливает новое значение ulimit.
 */
static int change_ulimit(const char *str)
{
    long value;

    if (parse_number(str, &value) == -1)
    {
        fprintf(stderr, "Invalid ulimit value: %s\n", str);
        return -1;
    }

    errno = 0;

    if (ulimit(UL_SETFSIZE, value) == -1 && errno != 0)
    {
        perror("ulimit");
        return -1;
    }

    return 0;
}


/*
 * Обрабатывает опцию -c.
 * Получает текущий soft limit на размер core-файла
 * и печатает его в байтах.
 */
static int print_core_size(void)
{
    struct rlimit limit;

    if (getrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("getrlimit");
        return -1;
    }

    if (limit.rlim_cur == RLIM_INFINITY)
    {
        printf("unlimited\n");
    }
    else
    {
        printf("%lu\n", (unsigned long)limit.rlim_cur);
    }

    return 0;
}


/*
 * Обрабатывает опцию -C.
 * Устанавливает новое ограничение на размер core-файла.
 * Размер задается в байтах.
 */
static int change_core_size(const char *str)
{
    struct rlimit limit;
    long value;

    if (parse_number(str, &value) == -1)
    {
        fprintf(stderr, "Invalid core size: %s\n", str);
        return -1;
    }

    if (getrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("getrlimit");
        return -1;
    }

    limit.rlim_cur = (rlim_t)value;

    if (setrlimit(RLIMIT_CORE, &limit) == -1)
    {
        perror("setrlimit");
        return -1;
    }

    return 0;
}


/*
 * Обрабатывает опцию -d.
 * Получает и печатает текущую рабочую директорию процесса.
 */
static int print_directory(void)
{
    char path[PATH_MAX];

    if (getcwd(path, sizeof(path)) == NULL)
    {
        perror("getcwd");
        return -1;
    }

    printf("%s\n", path);

    return 0;
}


/*
 * Обрабатывает опцию -v.
 * Печатает все переменные среды текущего процесса.
 */
static void print_environment(void)
{
    char **env;

    for (env = environ; *env != NULL; env++)
    {
        printf("%s\n", *env);
    }
}


/*
 * Обрабатывает опцию -V.
 * Добавляет новую переменную среды или изменяет существующую.
 * Ожидаемый формат аргумента: NAME=VALUE.
 */
static int change_environment(char *str)
{
    char *equal;

    equal = strchr(str, '=');

    if (equal == NULL || equal == str)
    {
        fprintf(stderr, "Invalid environment variable: %s\n", str);
        return -1;
    }

    if (putenv(str) == -1)
    {
        perror("putenv");
        return -1;
    }

    return 0;
}


/*
 * Освобождает память, выделенную под список опций.
 */
static void free_options(Option *option)
{
    Option *next;

    while (option != NULL)
    {
        next = option->next;
        free(option);
        option = next;
    }
}


int main(int argc, char *argv[])
{
    Option *options = NULL;
    Option *node;
    Option *current;

    int opt;
    int status = 0;

    /*
     * Отключаем стандартные сообщения getopt,
     * чтобы самостоятельно обрабатывать ошибки.
     */
    opterr = 0;

    /*
     * getopt разбирает аргументы слева направо.
     *
     * Каждую найденную опцию мы добавляем
     * в начало связного списка.
     *
     * Поэтому итоговый список автоматически
     * оказывается в обратном порядке,
     * как и требует задание: справа налево.
     */
    while ((opt = getopt(argc, argv, ":ispuU:cC:dvV:")) != -1)
    {
        if (opt == '?')
        {
            fprintf(stderr, "Unknown option: -%c\n", optopt);
            free_options(options);
            return 1;
        }

        if (opt == ':')
        {
            fprintf(stderr,
                    "Option -%c requires an argument\n",
                    optopt);

            free_options(options);
            return 1;
        }

        node = malloc(sizeof(Option));

        if (node == NULL)
        {
            perror("malloc");
            free_options(options);
            return 1;
        }

        node->code = opt;
        node->arg = optarg;

        node->next = options;
        options = node;
    }

    /*
     * Выполняем сохраненные опции.
     *
     * Так как список был построен в обратном порядке,
     * действия выполняются справа налево.
     */
    current = options;

    while (current != NULL)
    {
        switch (current->code)
        {
            case 'i':
                print_ids();
                break;

            case 's':
                if (make_group_leader() == -1)
                {
                    status = 1;
                }
                break;

            case 'p':
                print_process_ids();
                break;

            case 'u':
                if (print_ulimit() == -1)
                {
                    status = 1;
                }
                break;

            case 'U':
                if (change_ulimit(current->arg) == -1)
                {
                    status = 1;
                }
                break;

            case 'c':
                if (print_core_size() == -1)
                {
                    status = 1;
                }
                break;

            case 'C':
                if (change_core_size(current->arg) == -1)
                {
                    status = 1;
                }
                break;

            case 'd':
                if (print_directory() == -1)
                {
                    status = 1;
                }
                break;

            case 'v':
                print_environment();
                break;

            case 'V':
                if (change_environment(current->arg) == -1)
                {
                    status = 1;
                }
                break;
        }

        current = current->next;
    }

    free_options(options);

    return status;
}