#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "cgrp-plugin.h"

static gboolean notify_cb  (GIOChannel *, GIOCondition, gpointer);
static void     subscr_init(cgrp_context_t *);
static void     subscr_exit(cgrp_context_t *);


typedef struct {
    list_hook_t   hook;
    void        (*cb)(cgrp_context_t *, cgrp_process_t *, char *, void *);
    void         *data;
} notif_handler_t;



/********************
 * notify_init
 ********************/
int
notify_init(cgrp_context_t *ctx, int port)
{
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if ((ctx->notifsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
        bind(ctx->notifsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OHM_ERROR("cgrp: failed to initialize notification socket");
        goto fail;
    }
    
    if ((ctx->notifchnl = g_io_channel_unix_new(ctx->notifsock)) == NULL) {
        OHM_ERROR("cgrp: failed to allocate watch for notification socket");
        goto fail;
    }
    
    ctx->notifsrc = g_io_add_watch(ctx->notifchnl, G_IO_IN, notify_cb, ctx);

    subscr_init(ctx);
    
    return TRUE;

 fail:
    OHM_ERROR("cgrp: failed to initialize notification socket");
    if (ctx->notifsock >= 0) {
        close(ctx->notifsock);
        ctx->notifsock = -1;
    }
    return FALSE;
}


/********************
 * notify_exit
 ********************/
void
notify_exit(cgrp_context_t *ctx)
{
    
    if (ctx->notifsrc) {
        g_source_remove(ctx->notifsrc);
        g_io_channel_unref(ctx->notifchnl);
        close(ctx->notifsock);
    }

    ctx->notifsrc  = 0;
    ctx->notifchnl = NULL;
    ctx->notifsock = -1;

    subscr_exit(ctx);
}


/********************
 * notify_subscribe
 ********************/
void
notify_subscribe(cgrp_context_t *ctx,
                 void (*cb)(cgrp_context_t *, cgrp_process_t *, char *, void *),
                 void *data)
{
    notif_handler_t *handler;

    if (ALLOC_OBJ(handler) == NULL) {
        OHM_ERROR("cgrp: failed to allocate notification handler");
        return;
    }

    handler->cb   = cb;
    handler->data = data;
    
    list_append(&ctx->notifsubscr, &handler->hook);
}


/********************
 * subscr_init
 ********************/
static void
subscr_init(cgrp_context_t *ctx)
{
    list_init(&ctx->notifsubscr);
}


/********************
 * subscr_exit
 ********************/
static void
subscr_exit(cgrp_context_t *ctx)
{
    list_hook_t     *p, *n;
    notif_handler_t *handler;
    
    list_foreach(&ctx->notifsubscr, p, n) {
        handler = list_entry(p, notif_handler_t, hook);
        list_delete(&handler->hook);
        FREE(handler);
    }
}


/********************
 * notify_subscribers
 ********************/
static void
notify_subscribers(cgrp_context_t *ctx, cgrp_process_t *process, char *state)
{
    notif_handler_t *handler;
    list_hook_t     *p, *n;

    list_foreach(&ctx->notifsubscr, p, n) {
        handler = list_entry(p, notif_handler_t, hook);
        handler->cb(ctx, process, state, handler->data);
    }
}


/********************
 * notify_cb
 ********************/
static gboolean
notify_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    cgrp_group_t   *prev_active, *curr_active;
    cgrp_process_t *process;
    
    char            buf[256], *pidp, *state;
    pid_t           pid;
    int             size;
    
    (void)chnl;
    (void)data;

    if (!(mask & G_IO_IN))
        return TRUE;
    
    size = sizeof(buf) - 1;
    if ((size = recv(ctx->notifsock, buf, size, MSG_DONTWAIT)) < 0) {
        OHM_ERROR("cgrp: failed to receive application notification");
        goto out;
    }
    
    buf[size] = '\0';
    OHM_DEBUG(DBG_NOTIFY, "got active/standby notification: '%s'", buf);

    process     = NULL;
    prev_active = ctx->active_group;
    pidp = buf;
    while (pidp && *pidp) {
        pid = (unsigned short)strtoul(pidp, &state, 10);
            
        if (*state == ' ')
            state++;
        else {
            OHM_ERROR("cgrp: received malformed notification '%s'", buf);
            goto out;
        }

        if ((pidp = strpbrk(state, "\r\n ")) != NULL)
            *pidp++ = '\0';

        process = proc_hash_lookup(ctx, pid);
        
        OHM_DEBUG(DBG_NOTIFY, "process <%u,%s> is now in state <%s>",
                  pid, process ? process->binary : "unknown", state);
        
        process_update_state(ctx, process, state);
    }

    curr_active = ctx->active_group;
    notify_group_change(ctx, prev_active, curr_active);

    notify_subscribers(ctx, process, state);
    
 out:
    return TRUE;
}


/********************
 * notify_group_change
 ********************/
int
notify_group_change(cgrp_context_t *ctx, cgrp_group_t *prev, cgrp_group_t *curr)
{
    char *group;
    char *vars[2*2 + 1];

    if (prev == curr)
        return TRUE;

    OHM_DEBUG(DBG_NOTIFY, "active group has changed from '%s' to '%s'",
              prev ? prev->name : "<none>", curr ? curr->name : "<none>");

    if (curr != NULL)
        group = curr->name;
    else
        group = "<none>";
    
    vars[0] = "cgroup_group";
    vars[1] = group;
    vars[2] = "cgroup_state";
    vars[3] = APP_ACTIVE;
    vars[4] = NULL;
    
    return ctx->resolve("cgroup_notify", vars) == 0;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
