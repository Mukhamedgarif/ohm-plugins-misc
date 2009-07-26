#include "cgrp-plugin.h"


/*
 * structures representing policy decisions
 */

typedef struct {                        /* reparent a group to a partition */
    char *group;
    char *partition;
} reparent_t;

typedef struct {                        /* freeze/unfreeze a partition */
    char *partition;
    char *state;
} freeze_t;

typedef void (*ep_cb_t) (GObject *, GObject *, gboolean);

static void     policy_decision (GObject *, GObject *, ep_cb_t, gpointer);
static void     policy_keychange(GObject *, GObject *, gpointer);
static gboolean txparser        (GObject *, GObject *, gpointer);


/********************
 * ep_init
 ********************/
int
ep_init(cgrp_context_t *ctx, GObject *(*signaling_register)(gchar *))
{
    if ((ctx->store = ohm_get_fact_store()) == NULL) {
        OHM_ERROR("cgrp: failed to initalize factstore");
        return FALSE;
    }
    
    if (signaling_register == NULL) {
        OHM_ERROR("cgrp: signaling interface not available");
        return FALSE;
    }

    if ((ctx->sigconn = signaling_register("cgroups")) == NULL) {
        OHM_ERROR("cgrp: failed to register for policy decisions");
        return FALSE;
    }

    ctx->sigdcn = g_signal_connect(ctx->sigconn, "on-decision",
                                   G_CALLBACK(policy_decision),  (gpointer)ctx);
    ctx->sigkey = g_signal_connect(ctx->sigconn, "on-key-change",
                                   G_CALLBACK(policy_keychange), (gpointer)ctx);
    
    return TRUE;
}


/********************
 * ep_exit
 ********************/
void
ep_exit(cgrp_context_t *ctx, gboolean (*signaling_unregister)(GObject *ep))
{
    ctx->store = NULL;
    
    if (signaling_unregister == NULL || ctx->sigconn == NULL)
        return;

    g_signal_handler_disconnect(ctx->sigconn, ctx->sigdcn);
    g_signal_handler_disconnect(ctx->sigconn, ctx->sigkey);
    
#if 0 /* Hmm... this seems to crash in the signaling plugin. Is it possible
       * that this triggers a bug that causes crashes if there are more than
       * 1 enforcement points ? */
    signaling_unregister(ctx->sigconn);
#endif
    ctx->sigconn = NULL;
}


/********************
 * policy_decision
 ********************/
static void
policy_decision(GObject *ep, GObject *tx, ep_cb_t callback, gpointer data)
{
    gboolean success;
    
    success = txparser(ep, tx, data);
    callback(ep, tx, success);
}


/********************
 * policy_keychange
 ********************/
static void
policy_keychange(GObject *ep, GObject *tx, gpointer data)
{
    txparser(ep, tx, data);
}


/********************
 * reparent_action
 ********************/
static int
reparent_action(cgrp_context_t *ctx, void *data)
{
    reparent_t       *action = (reparent_t *)data;
    cgrp_group_t     *group;
    cgrp_partition_t *partition;
    int               success;

    if ((group = group_lookup(ctx, action->group)) == NULL) {
        OHM_WARNING("cgrp: ignoring reparenting of unknown group '%s'",
                    action->group);
        return TRUE;
    }

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_ERROR("cgrp: cannot reparent unknown partition '%s'",
                  action->partition);
        return FALSE;
    }
    
    success = partition_group(partition, group);

    OHM_DEBUG(DBG_ACTION, "reparenting group '%s' to partition '%s' %s",
              action->group, action->partition, success ? "OK" : "FAILED");

    return success;
}


/********************
 * freeze_action
 ********************/
static int
freeze_action(cgrp_context_t *ctx, void *data)
{
    freeze_t         *action = (freeze_t *)data;
    int               frozen = !strcmp(action->state, "frozen");
    cgrp_partition_t *partition;
    int               success;

    if ((partition = partition_lookup(ctx, action->partition)) == NULL) {
        OHM_WARNING("cgrp: ignoring %sfreezing of unknown partition '%s'",
                    frozen ? "" : "un", action->partition);
        return TRUE;
    }
    
    success = partition_freeze(partition, frozen);

    OHM_DEBUG(DBG_ACTION, "%sfreeze partition '%s': %s", frozen ? "" : "un",
              action->partition, success ? "OK" : "FAILED");

    return success;
}


/*****************************************************************************
 *                  *** action parsing routines from videoep ***             *
 *****************************************************************************/

#define PREFIX   "com.nokia.policy."
#define REPARENT PREFIX"cgroup_partition"
#define FREEZE   PREFIX"partition_freeze"
#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

typedef int (*action_t)(cgrp_context_t *, void *);

typedef enum {
    argtype_invalid = 0,
    argtype_string,
    argtype_integer,
    argtype_unsigned
} argtype_t;

typedef struct {		/* argument descriptor for actions */
    argtype_t   type;
    const char *name;
    int         offs;
} argdsc_t; 

typedef struct {		/* action descriptor */
    const char *name;
    action_t    handler;
    argdsc_t   *argdsc;
    int         datalen;
} actdsc_t;

static argdsc_t reparent_args [] = {
    { argtype_string , "group"    , STRUCT_OFFSET(reparent_t, group)     },
    { argtype_string , "partition", STRUCT_OFFSET(reparent_t, partition) },
    { argtype_invalid,  NULL      , 0                                     }
};

static argdsc_t freeze_args [] = {
    { argtype_string , "partition", STRUCT_OFFSET(freeze_t, partition) },
    { argtype_string , "state"    , STRUCT_OFFSET(freeze_t, state)     },
    { argtype_invalid,  NULL      , 0                                  }
};

static actdsc_t actions[] = {
    { REPARENT, reparent_action, reparent_args, sizeof(reparent_t) },
    { FREEZE  , freeze_action  , freeze_args  , sizeof(freeze_t)   },
    { NULL    , NULL           , NULL         , 0                  }
};

static int action_parser  (actdsc_t *, cgrp_context_t *);
static int get_args       (OhmFact *, argdsc_t *, void *);


static gboolean
txparser(GObject *conn, GObject *transaction, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    guint      txid;
    GSList    *entry, *list;
    char      *name;
    actdsc_t  *action;
    gboolean   success;
    gchar     *signal;

    (void)conn;

    g_object_get(transaction, "txid" , &txid, NULL);
    g_object_get(transaction, "facts", &list, NULL);
    g_object_get(transaction, "signal", &signal, NULL);
    
    /* TODO: signal holds the signal name that was sent */

    success = TRUE;

    for (entry = list; entry != NULL; entry = g_slist_next(entry)) {
        name = (char *)entry->data;
        for (action = actions; action->name != NULL; action++) {
            if (!strcmp(name, action->name))
                success &= action_parser(action, ctx);
        }
    }

    g_free(signal);
    
    return success;
}

static int action_parser(actdsc_t *action, cgrp_context_t *ctx)
{
    OhmFact *fact;
    GSList  *list;
    char    *data;
    int      success;

    if ((data = malloc(action->datalen)) == NULL) {
        OHM_ERROR("Can't allocate %d byte memory", action->datalen);

        return FALSE;
    }

    success = TRUE;

    for (list  = ohm_fact_store_get_facts_by_name(ctx->store, action->name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        memset(data, 0, action->datalen);

        if (!get_args(fact, action->argdsc, data))
            success &= FALSE;
        else
            success &= action->handler(ctx, data);
    }

    free(data);
    
    return success;
}

static int get_args(OhmFact *fact, argdsc_t *argdsc, void *args)
{
    argdsc_t *ad;
    GValue   *gv;
    void     *vptr;

    if (fact == NULL)
        return FALSE;

    for (ad = argdsc;    ad->type != argtype_invalid;   ad++) {
        vptr = args + ad->offs;

        if ((gv = ohm_fact_get(fact, ad->name)) == NULL)
            continue;

        switch (ad->type) {

        case argtype_string:
            if (G_VALUE_TYPE(gv) == G_TYPE_STRING)
                *(const char **)vptr = g_value_get_string(gv);
            break;

        case argtype_integer:
            if (G_VALUE_TYPE(gv) == G_TYPE_LONG)
                *(long *)vptr = g_value_get_long(gv);
            break;

        case argtype_unsigned:
            if (G_VALUE_TYPE(gv) == G_TYPE_ULONG)
                *(unsigned long *)vptr = g_value_get_ulong(gv);
            break;

        default:
            break;
        }
    }

    return TRUE;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */