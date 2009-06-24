/* libep -- a library for receiving policy decisions from the policy manager */

#ifndef LIBEP_H
#define LIBEP_H

#include <stdlib.h>
#include <dbus/dbus.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#define POLICY_DBUS_INTERFACE   "com.nokia.policy"
#define POLICY_DBUS_PATH        "/com/nokia/policy"
#define POLICY_DBUS_NAME        "org.freedesktop.ohm"
#define POLICY_DECISION         "decision"
#define POLICY_STATUS           "status"

/* As simple API as possible: those wanting to do more difficult things
 * can use the D-Bus API directly. */

enum ep_value_type {
    EP_VALUE_INVALID = 0,
    EP_VALUE_INT,
    EP_VALUE_FLOAT,
    EP_VALUE_STRING
};

struct ep_key_value_pair {
    char *key;
    enum ep_value_type type;
    void *value;
};

struct ep_decision {
    struct ep_key_value_pair **pairs;
};


/* callbacks and such */

typedef int     ep_answer_token;
typedef void    (*ep_answer_cb) (ep_answer_token token, int success);
typedef void    (*ep_decision_cb) (const char *decision_name, 
        struct ep_decision **decisions, ep_answer_cb cb, ep_answer_token token,
        void *user_data);



/* functions for registering and unregistering to the policy engine */

int ep_register     (DBusConnection *connection, const char *name);
int ep_unregister   (DBusConnection *connection);


/* function for setting up the policy decision filter */

int ep_filter   (const char **decision_names, const char *signal, 
        ep_decision_cb cb, void *user_data);


/* functions for handling the decision structures */

int ep_decision_has_key             (struct ep_decision *decision, const char *key);
enum ep_value_type ep_decision_type (struct ep_decision *decision, const char *key);


/* getters for the different value types */

const char * ep_decision_get_string (struct ep_decision *decision, const char *key);
int ep_decision_get_int             (struct ep_decision *decision, const char *key);
double ep_decision_get_float        (struct ep_decision *decision, const char *key);

#endif
