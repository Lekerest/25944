#include <stdio.h> // стандарт ввод/вывод принтф перрор
#include <stdlib.h> // стандартная библиотека для strtol
#include <unistd.h> // всякая всячина из юникса, в том числе getopt
#include <sys/resource.h> // getrlimit и setrlimit ограничение ресурсов процессора
#include <ulimit.h> // функции ulimit
#include <errno.h> // глобальная переменная errno для обработки ошибок
#include <limits.h> // PATH_MAX

extern char **environ; // массив строк окружения path home user etc


/*
 * Преобразует строку в неотрицательное число.
 * Используется для -U и -C.
 */
static int get_number(const char *str, long *value)
{
    char *end; // сюда strtol запишет, где закончил читать число

    errno = 0; // сбрасываем старую ошибку
    *value = strtol(str, &end, 10); // преобразуем строку в long, основание 10

    if (errno != 0 || end == str || *end != '\0' || *value < 0) // проверяем ошибки и мусор после числа
    {
        return -1; // ошибка
    }

    return 0; // успешно
}


/*
 * Читает опции с помощью getopt().
 * Рекурсивный вызов позволяет выполнить их
 * в обратном порядке: справа налево.
 */
static int process_options(int argc, char *argv[])
{
    int option; // текущая опция
    int bad_option; // неизвестная опция или опция без аргумента
    int status; // 0 - успешно, 1 - была ошибка
    char *argument; // аргумент опции U, C или V

    long value; // число для U и C
    struct rlimit limit; // лимиты ресурсов процесса
    char path[PATH_MAX]; // буфер для текущей директории
    char **env; // указатель для прохода по environ

    option = getopt(argc, argv, ":ispuU:cC:dvV:"); // читаем очередную опцию

    if (option == -1) // опций больше нет
    {
        return 0;
    }

    /*
     * Сохраняем значения, потому что следующий
     * вызов getopt() изменит optarg и optopt.
     */
    argument = optarg; // сохраняем аргумент текущей опции
    bad_option = optopt; // сохраняем ошибочную опцию

    /*
     * Сначала обрабатываем все опции справа.
     */
    status = process_options(argc, argv); // рекурсивно идём до самой правой опции

    /*
     * После возврата выполняем текущую опцию.
     */
    switch (option)
    {
        case 'i':
            printf("Real UID: %ld\n", (long)getuid()); // реальный UID
            printf("Effective UID: %ld\n", (long)geteuid()); // эффективный UID
            printf("Real GID: %ld\n", (long)getgid()); // реальный GID
            printf("Effective GID: %ld\n", (long)getegid()); // эффективный GID
            break;

        case 's':
            if (setpgid(0, 0) == -1) // делаем текущий процесс лидером группы
            {
                perror("setpgid");
                status = 1;
            }
            break;

        case 'p':
            printf("PID: %ld\n", (long)getpid()); // PID текущего процесса
            printf("PPID: %ld\n", (long)getppid()); // PID родителя
            printf("PGID: %ld\n", (long)getpgrp()); // ID группы процессов
            break;

        case 'u':
            errno = 0; // сбрасываем errno
            value = ulimit(UL_GETFSIZE); // получаем текущий ulimit размера файла

            if (value == -1 && errno != 0) // проверяем ошибку
            {
                perror("ulimit");
                status = 1;
            }
            else
            {
                printf("%ld\n", value); // печатаем ulimit
            }
            break;

        case 'U':
            if (get_number(argument, &value) == -1) // переводим аргумент U в число
            {
                fprintf(stderr, "Invalid ulimit value: %s\n", argument);
                status = 1;
            }
            else
            {
                errno = 0;

                if (ulimit(UL_SETFSIZE, value) == -1 && errno != 0) // устанавливаем новый ulimit
                {
                    perror("ulimit");
                    status = 1;
                }
            }
            break;

        case 'c':
            if (getrlimit(RLIMIT_CORE, &limit) == -1) // получаем лимит размера core-файла
            {
                perror("getrlimit");
                status = 1;
            }
            else if (limit.rlim_cur == RLIM_INFINITY) // если лимита нет
            {
                printf("unlimited\n");
            }
            else
            {
                printf("%llu\n",
                       (unsigned long long)limit.rlim_cur); // печатаем soft limit в байтах
            }
            break;

        case 'C':
            if (get_number(argument, &value) == -1) // переводим size в число
            {
                fprintf(stderr, "Invalid core size: %s\n", argument);
                status = 1;
            }
            else if (getrlimit(RLIMIT_CORE, &limit) == -1) // получаем текущие лимиты
            {
                perror("getrlimit");
                status = 1;
            }
            else
            {
                limit.rlim_cur = (rlim_t)value; // меняем soft limit

                if (setrlimit(RLIMIT_CORE, &limit) == -1) // устанавливаем новый core limit
                {
                    perror("setrlimit");
                    status = 1;
                }
            }
            break;

        case 'd':
            if (getcwd(path, sizeof(path)) == NULL) // получаем текущую директорию
            {
                perror("getcwd");
                status = 1;
            }
            else
            {
                printf("%s\n", path);
            }
            break;

        case 'v':
            for (env = environ; *env != NULL; env++) // перебираем всё окружение
            {
                printf("%s\n", *env); // печатаем одну переменную окружения
            }
            break;

        case 'V':
            if (putenv(argument) == -1) // добавляем или изменяем NAME=VALUE
            {
                perror("putenv");
                status = 1;
            }
            break;

        case '?':
            fprintf(stderr, "Unknown option: -%c\n", bad_option); // неизвестная опция
            status = 1;
            break;

        case ':':
            fprintf(stderr,
                    "Option -%c requires an argument\n",
                    bad_option); // для U, C или V не передали аргумент
            status = 1;
            break;
    }

    return status; // возвращаем итоговый код программы
}


int main(int argc, char *argv[])
{
    opterr = 0; // запрещаем getopt самому печатать ошибки

    return process_options(argc, argv); // запускаем обработку всех опций
}