#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"
#include "cgrp-plugin.h"

#ifdef HAVE_OSSO_SWAP_PRESSURE
#include <ioq-notify.h>
#endif


typedef struct {
    int  (*init)(cgrp_context_t *);
    void (*exit)(cgrp_context_t *);
} sysmon_t;


static int  iow_init(cgrp_context_t *ctx);
static void iow_exit(cgrp_context_t *ctx);
static int  swp_init(cgrp_context_t *ctx);
static void swp_exit(cgrp_context_t *ctx);

static void          estim_free(estim_t *);
static unsigned long estim_update(estim_t *, unsigned long);


static sysmon_t monitors[] = {
    { iow_init, iow_exit },
    { swp_init, swp_exit },
    { NULL    , NULL     }
};

static int clkhz;



/********************
 * sysmon_init
 ********************/
int
sysmon_init(cgrp_context_t *ctx)
{
    sysmon_t *mon;
    
    clkhz          = sysconf(_SC_CLK_TCK);
    ctx->proc_stat = open("/proc/stat", O_RDONLY);

    if (ctx->proc_stat < 0) {
        OHM_ERROR("cgrp: failed to open /proc/stat");
        return FALSE;
    }
    
    for (mon = monitors; mon->init != NULL; mon++)
        mon->init(ctx);
    
    return TRUE;
}


/********************
 * sysmon_exit
 ********************/
void
sysmon_exit(cgrp_context_t *ctx)
{
    sysmon_t *mon;

    for (mon = monitors; mon->init != NULL; mon++)
        if (mon->exit != NULL)
            mon->exit(ctx);
    
    if (ctx->proc_stat >= 0) {
        close(ctx->proc_stat);
        ctx->proc_stat = -1;
    }
}



/*****************************************************************************
 *                  *** polling I/O-wait state monitoring ***                *
 *****************************************************************************/
#define DEFAULT_STARTUP_DELAY 120

static gboolean iow_calculate(gpointer ptr);
static gboolean iow_sample(int fd, unsigned long *sample, timestamp_t *stamp);


/********************
 * iow_init
 ********************/
static int
iow_init(cgrp_context_t *ctx)
{
    cgrp_iowait_t *iow = &ctx->iow;

    if (iow->thres_low == 0 && iow->thres_high == 0) {
        OHM_INFO("cgrp: I/O-wait state monitoring disabled");
        return TRUE;
    }

    if (iow->thres_high < iow->thres_low) {
        OHM_ERROR("cgrp: invalid I/O-wait threshold %u-%u",
                  iow->thres_low, iow->thres_high);
        return TRUE;
    }

    if (iow->estim == NULL) {
        OHM_INFO("cgrp: missing/invalid I/O wait estimator, disabling");
        return TRUE;
    }
    
    if (!iow->startup_delay)
        iow->startup_delay = DEFAULT_STARTUP_DELAY;

    OHM_INFO("cgrp: I/O wait notification enabled");
    OHM_INFO("cgrp: threshold %u-%u, poll %u-%u, %s %u, hook %s, "
             "startup delay %u",
             iow->thres_low, iow->thres_high,
             iow->poll_low, iow->poll_high,
             iow->estim->type == ESTIM_TYPE_WINDOW ? "window" : "ewma",
             iow->nsample, iow->hook,
             iow->startup_delay);

    iow_sample(ctx->proc_stat, &iow->sample, &iow->stamp);
    iow->timer = g_timeout_add(1000 * iow->startup_delay, iow_calculate, ctx);
    
    return TRUE;
}


/********************
 * iow_exit
 ********************/
static void
iow_exit(cgrp_context_t *ctx)
{
    if (ctx->iow.timer != 0) {
        g_source_remove(ctx->iow.timer);
        ctx->iow.timer = 0;
    }

    estim_free(ctx->iow.estim);
    ctx->iow.estim = NULL;
    FREE(ctx->iow.hook);
    ctx->iow.hook = NULL;
}


/********************
 * iow_notify
 ********************/
static int
iow_notify(cgrp_context_t *ctx)
{
    char *vars[2 + 1];
    char *state;

    state = ctx->iow.alert ? "high" : "low";

    vars[0] = "iowait";
    vars[1] = state;
    vars[2] = NULL;
    
    OHM_DEBUG(DBG_SYSMON, "I/O wait %s notification", state);

    return ctx->resolve(ctx->iow.hook, vars) == 0;
}


/********************
 * iow_sample
 ********************/
static gboolean
iow_sample(int fd, unsigned long *sample, timestamp_t *stamp)
{
    unsigned long usr, nic, sys, idl, iow;
    char          buf[256], *p, *e;
    int           n;

    n = read(fd, buf, sizeof(buf) - 1);
    lseek(fd, SEEK_SET, 0);

    if (n < 4 || strncmp(buf, "cpu ", 4)) {
        OHM_ERROR("failed to read /proc/stat");
        return FALSE;
    }

    buf[n] = '\0';
    p = buf + 4;
    n -= 4;

    while (*p == ' ')
        p++;

    usr = strtoul(p, &e, 10); if (*e) p = e + 1;
    nic = strtoul(p, &e, 10); if (*e) p = e + 1;
    sys = strtoul(p, &e, 10); if (*e) p = e + 1;
    idl = strtoul(p, &e, 10); if (*e) p = e + 1;
    iow = strtoul(p, &e, 10);

    if (*e != ' ')
        return FALSE;

    *sample = iow;
    clock_gettime(CLOCK_MONOTONIC, stamp);
    
    return TRUE;
}


/********************
 * msec_diff
 ********************/
static inline unsigned long
msec_diff(struct timespec *now, struct timespec *prv)
{
    unsigned long diff;

    diff = (now->tv_sec - prv->tv_sec) * 1000;
    
    if (now->tv_nsec >= prv->tv_nsec)
        diff += (now->tv_nsec - prv->tv_nsec) / (1000 * 1000);
    else
        diff -= (prv->tv_nsec - now->tv_nsec) / (1000 * 1000);

    return diff;
}


/********************
 * iow_schedule
 ********************/
void
iow_schedule(cgrp_context_t *ctx, unsigned long iow)
{
    unsigned int delay;
    double       u;
    
    if (iow <= ctx->iow.thres_low)
        delay = ctx->iow.poll_high;
    else if (iow >= ctx->iow.thres_high)
        delay = ctx->iow.poll_low;
    else {
        u  = ctx->iow.poll_high - ctx->iow.poll_low;
        u /= ctx->iow.thres_high - ctx->iow.thres_low;
        
        delay = (ctx->iow.poll_high - (iow - ctx->iow.thres_low) * u + 0.5);
    }

    ctx->iow.timer = g_timeout_add(1000 * delay, iow_calculate, ctx);
    OHM_DEBUG(DBG_SYSMON, "scheduled I/O wait sampling after %d sec", delay);
}


/********************
 * iow_calculate
 ********************/
static gboolean
iow_calculate(gpointer ptr)
{
    cgrp_context_t *ctx = (cgrp_context_t *)ptr;
    cgrp_iowait_t  *iow = &ctx->iow;
    unsigned long   prevs, ds, dt, rate, avg;
    timestamp_t     prevt;
    
    prevs = iow->sample;
    prevt = iow->stamp;
    iow_sample(ctx->proc_stat, &iow->sample, &iow->stamp);

    dt   = msec_diff(&iow->stamp, &prevt);          /* sample period */
    ds   = (iow->sample - prevs) * 1000 / clkhz;    /* sample diff   */
    rate = ds * 1000 / dt;                    /* normalized to 1 sec */
        
    avg = estim_update(iow->estim, rate);
    
    OHM_DEBUG(DBG_SYSMON, "I/O wait sample %.2f %%, average %.2f %%",
              (100.0 * rate) / 1000, (100.0 * avg) / 1000.0);

    avg = (100 * avg) / 1000;

    if (iow->alert) {
        if (avg < (unsigned long)iow->thres_low) {
            iow->alert = FALSE;
            iow_notify(ctx);
        }
    }
    else {
        if (avg >= (unsigned long)iow->thres_high) {
                iow->alert = TRUE;
                iow_notify(ctx);
        }
    }
    
    iow_schedule(ctx, avg);
    return FALSE;
}


/*****************************************************************************
 *                     *** OSSO swap pressure monitoring ***                 *
 *****************************************************************************/


#ifdef HAVE_OSSO_SWAP_PRESSURE
/********************
 * swp_notify
 ********************/
static void
swp_notify(const osso_ioq_activity_t level, void *data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    char           *vars[2 + 1];
    char           *state;

    state = (level == ioq_activity_high) ? "high" : "low";

    vars[0] = "iowait";
    vars[1] = state;
    vars[2] = NULL;
    
    OHM_DEBUG(DBG_SYSMON, "swap pressure %s notification", state);

    ctx->resolve(ctx->swp.hook, vars);
}


/********************
 * swp_init
 ********************/
static int
swp_init(cgrp_context_t *ctx)
{
    if (ctx->swp.low != 0 || ctx->swp.high != 0)
        OHM_WARNING("cgrp: swap pressure thresholds currently ignored!");
    
    if (ctx->swp.hook != NULL)
        return osso_ioq_notify_init(swp_notify, ctx, 5 * 1000);
    else
        return 0;
}


/********************
 * swp_exit
 ********************/
static void
swp_exit(cgrp_context_t *ctx)
{
    if (ctx->swp.hook != NULL)
        osso_ioq_notify_deinit();
}


#else /* !HAVE_OSSO_SWAP_PRESSURE */


static int
swp_init(cgrp_context_t *ctx)
{
    (void)ctx;
    
    if (ctx->swp.hook != NULL)
        OHM_WARNING("cgrp: no support for swap pressure monitoring!");
    
    return 0;
}


static void
swp_exit(cgrp_context_t *ctx)
{
    (void)ctx;
}
#endif



/*****************************************************************************
 *                          *** estimator routines ***                       *
 *****************************************************************************/

static ewma_t        *ewma_alloc   (int nsample);
static unsigned long  ewma_update  (ewma_t *ewma, unsigned long item);
static void           ewma_free    (ewma_t *ewma);
static sldwin_t      *sldwin_alloc (int nsample);
static void           sldwin_free  (sldwin_t *win);
static unsigned long  sldwin_update(sldwin_t *win, unsigned long item);


/********************
 * ewma_alloc
 ********************/
static ewma_t *
ewma_alloc(int nsample)
{
    ewma_t *ewma;
    
    if (nsample <= 0) {
        OHM_ERROR("cgrp: invalid number of samples for EWMA");
        return NULL;
    }
    
    if (ALLOC_OBJ(ewma) != NULL) {
        ewma->type = ESTIM_TYPE_EWMA;
        ewma->alpha = 2.0 / (1.0 * nsample + 1);
    }
    
    return ewma;
}


/********************
 * ewma_free
 ********************/
static void
ewma_free(ewma_t *ewma)
{
    FREE(ewma);
}


/********************
 * ewma_update
 ********************/
static unsigned long
ewma_update(ewma_t *ewma, unsigned long item)
{
    ewma->S = ewma->alpha * item + (1.0 - ewma->alpha) * ewma->S;
    return (unsigned long)(ewma->S + 0.5);
}


/********************
 * estim_alloc
 ********************/
estim_t *
estim_alloc(char *estimator, int nsample)
{
    estim_t *estim;

    if (!strcmp(estimator, "window"))
        estim = (estim_t *)sldwin_alloc(nsample);
    else if (!strcmp(estimator, "ewma"))
        estim = (estim_t *)ewma_alloc(nsample);
    else {
        OHM_ERROR("cgrp: invalid estimator type %s", estimator);
        return NULL;
    }
    
    return estim;
}


/********************
 * estim_free
 ********************/
static void
estim_free(estim_t *estim)
{
    switch (estim->type) {
    case ESTIM_TYPE_WINDOW: sldwin_free(&estim->win); break;
    case ESTIM_TYPE_EWMA:   ewma_free(&estim->ewma);  break;
    default:                                          break;
    }
}


/********************
 * estim_update
 ********************/
static unsigned long
estim_update(estim_t *estim, unsigned long sample)
{
    unsigned long avg;

    switch (estim->type) {
    case ESTIM_TYPE_WINDOW: avg = sldwin_update(&estim->win, sample); break;
    case ESTIM_TYPE_EWMA:   avg = ewma_update(&estim->ewma , sample); break;
    default:                avg = 0;
    }

    return avg;
}


/********************
 * sldwin_alloc
 ********************/
sldwin_t *
sldwin_alloc(int size)
{
    sldwin_t *win;
    int       bytes;

    bytes = (int)&((sldwin_t *)NULL)->items[size];
    win   = (sldwin_t *)ALLOC_ARR(char, bytes);

    if (win != NULL) {
        win->type  = ESTIM_TYPE_WINDOW;
        win->size  = size;
    }

    return win;
}


/********************
 * sldwin_free
 ********************/
void
sldwin_free(sldwin_t *win)
{
    FREE(win);
}


/********************
 * sldwin_update
 ********************/
unsigned long
sldwin_update(sldwin_t *win, unsigned long item)
{
    double avg;
    int    prev;
    
    if (win->ready) {
        prev = (win->idx + win->size) % win->size;
        win->total -= win->items[prev];
    }
    
    win->items[win->idx++]  = item;
    win->total             += item;
    
    if (win->ready)
        avg = (1.0 * win->total) / win->size;
    else
        avg = (1.0 * win->total) / win->idx;
    
    if (win->idx >= win->size) {
        win->ready = TRUE;
        win->idx   = 0;
    }

#undef __DUMP_WINDOW__    
#ifdef __DUMP_WINDOW__
    {
        int   i, min, max;
        char *t = "";
        
        if (!win->ready) {
            min = 0;
            max = win->idx;
        }
        else {
            min = win->idx;
            max = win->idx + win->size;
        }

        printf("added %lu: [", item);
        if (!win->ready) {
            for (i = 0; i < win->size - win->idx; i++, t=",")
                printf("%s%s", t, "-");
        }
            
        for (i = min; i < max; i++, t = ",")
            printf("%s%lu", t, win->items[i % win->size]);
        printf("], total = %lu, avg = %.2f\n", win->total, avg);
    }
#endif
    
    return (unsigned long)(avg + 0.5);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

