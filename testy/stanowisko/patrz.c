/* K1 — a host helper of the stand (0.11.0 step 2, testy/stanowisko/pobrane.py): prints inotify events of a folder (name, mask) — to see how Firefox finishes a download */
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>
int main(int c, char **v)
{
    char b[4096] __attribute__((aligned(8)));
    int in = inotify_init();
    if (c < 2 || inotify_add_watch(in, v[1], IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE) < 0)
        return 1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (;;) {
        ssize_t n = read(in, b, sizeof b);
        for (ssize_t o = 0; o < n;) {
            struct inotify_event *e = (struct inotify_event *)(b + o);
            printf("%s%s%s%s%s %s\n", e->mask & IN_CREATE ? "CREATE" : "", e->mask & IN_CLOSE_WRITE ? "CLOSE_WRITE" : "",
                   e->mask & IN_MOVED_TO ? "MOVED_TO" : "", e->mask & IN_MOVED_FROM ? "MOVED_FROM" : "",
                   e->mask & IN_DELETE ? "DELETE" : "", e->len ? e->name : "");
            o += (ssize_t)sizeof *e + e->len;
        }
    }
}
