#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

typedef int (*action_t)(videoep_t *, void *);

typedef enum {
    argtype_invalid = 0,
    argtype_string,
    argtype_integer,
    argtype_unsigned
} argtype_t;

typedef struct {		/* argument descriptor for actions */
    argtype_t     type;
    const char   *name;
    int           offs;
} argdsc_t; 

typedef struct {		/* action descriptor */
    const char   *name;
    action_t      handler;
    argdsc_t     *argdsc;
    int           datalen;
} actdsc_t;

typedef struct {                /* video routing data structure */
    char         *device;
} route_t;

static int route_action(videoep_t *, void *);
static int action_parser(actdsc_t *, videoep_t *);
static int get_args(OhmFact *, argdsc_t *, void *);


static gboolean txparser(GObject *conn, GObject *transaction, gpointer data)
{
#define PREFIX "com.nokia.policy."

    (void)conn;

    static argdsc_t  route_args [] = {
        {argtype_string  ,   "device",  STRUCT_OFFSET(route_t, device)},
        {argtype_invalid ,     NULL  ,                 0              }
    };

    static actdsc_t  actions[] = {
        {PREFIX "video_route", route_action, route_args, sizeof(route_t)},
        {           NULL     ,     NULL    ,    NULL   ,      0         }
    };
    
    videoep_t *videoep = data;
    guint      txid;
    GSList    *entry, *list;
    char      *name;
    actdsc_t  *action;
    gboolean   success;
    
    printf("Video EP: got actions\n");

    g_object_get(transaction, "txid" , &txid, NULL);
    g_object_get(transaction, "facts", &list, NULL);
    
    printf("txid: %d\n", txid);
    
    success = TRUE;

    for (entry = list;    entry != NULL;    entry = g_slist_next(entry)) {
        name = (char *)entry->data;

        for (action = actions;   action->name != NULL;   action++) {
            if (!strcmp(name, action->name))
                success &= action_parser(action, videoep);
        }
    }
    
    return success;

#undef PREFIX
}

static int route_action(videoep_t *videoep, void *data)
{
    route_t *route = data;

    printf("*** Got video route to '%s'\n", route->device);

    return TRUE;
}


static int action_parser(actdsc_t *action, videoep_t *videoep)
{
    (void)videoep;

    OhmFact *fact;
    GSList  *list;
    char    *data;
    int      success;

    if ((data = malloc(action->datalen)) == NULL) {
        OHM_ERROR("Can't allocate %d byte memory", action->datalen);

        return FALSE;
    }

    success = TRUE;

    for (list  = ohm_fact_store_get_facts_by_name(videoep->fs, action->name);
         list != NULL;
         list  = g_slist_next(list))
    {
        fact = (OhmFact *)list->data;

        memset(data, 0, action->datalen);

        if (!get_args(fact, action->argdsc, data))
            success &= FALSE;
        else
            success &= action->handler(videoep, data);
    }
    
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
