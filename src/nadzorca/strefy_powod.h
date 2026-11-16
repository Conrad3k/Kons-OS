/* K1 nadzorca — why a zone ended (0.10.1 step 1, S3): from the VMM's exit status and its sandbox's memory events.
 * Pure (test_powod_strefy on the host); strefy.c reads the events. */
#ifndef K1_STREFY_POWOD_H
#define K1_STREFY_POWOD_H

/* How the counters of the zone's cgroup (memory.events) changed while it ran; -1: they could not be read. */
struct strefa_zdarzenia {
    long long oom;      /* "oom": the cgroup reached its own memory.max (the session's limit) */
    long long oom_kill; /* "oom_kill": its processes killed by any OOM killer (its own limit or the computer's) */
};

/* 0: the user closed it (or its program closed normally); else K1_STREFA_KONIEC_*. */
int strefa_powod_konca(int status, const struct strefa_zdarzenia *d);

#endif
