:- module(telephony, [call_request/3]).

call_request(Id, State, [Actions]) :- call_actions(Id, State, Actions).


/*
 * Calls are always allowed to be put on hold or disconnected.
 */

call_actions(Id, onhold, [call_action, [Id, onhold]]) :- !.
call_actions(Id, disconnected, [call_action, [Id, disconnected]]) :- !.


/*
 * A new call can be created if none is alerting and we have less than 3 calls.
 */

call_actions(Id, created, [call_action, [Id, created]]) :-
    \+ has_alerting,
    number_of_calls(N),
    N < 3,
    !.


/*
 * A call can be simply activated if we have no active calls.
 */

call_actions(Id, active, [call_action, [Id, active]]) :-
    \+ has_active, !.


/*
 * Otherwise the active call must be put on hold first.
 */

call_actions(Id, active, Actions) :-
    find_calls(active, ActiveCalls),
    maplist(hold_call, ActiveCalls, HoldActions),
    activate_call(Id, ActivateActions),
    append(HoldActions, [ActivateActions], CallActions),
    append([call_action], CallActions, Actions), !.


/*
 * All other call requests are rejected disconnecting the affected call.
 */

call_actions(Id, _, [call_action, Action]) :- reject_call(Id, Action).


/* call actions */
create_call(Id, [Id, created]).
hold_call(Id, [Id, onhold]).
disconnect_call(Id, [Id, disconnected]).
reject_call(Id, [Id, disconnected]).
activate_call(Id, [Id, active]).


/* find calls in a given state */
find_calls(State, CallList) :-
    findall(Call,
            fact_exists('com.nokia.policy.call', [id, state], [Call, State]),
            CallList).

/* number of calls in a given state */
num_calls_in_state(State, N) :- find_calls(State, List), length(List, N).

/* number of all existing calls */
number_of_calls(N) :-
    num_calls_in_state(created, Created),
    num_calls_in_state(active, Active),
    num_calls_in_state(onhold, OnHold),
    N = Created + Active + OnHold.


/* call classification */

is_cellular_call(Id) :-
    fact_exists('com.nokia.policy.call', [id, path], [Id, Path]),
    is_cellular_path(Path).

is_ip_call(Id) :-
    fact_exists('com.nokia.policy.call', [id, path], [Id, Path]),
    \+ is_cellular_path(Path).

is_cellular_path(P) :-
    sub_string(P,
               0, 52, _,
               '/org/freedesktop/Telepathy/Connection/ring/tel/ring/').

has_alerting :-
    fact_exists('com.nokia.policy.call', [state, dir], [created, incoming]).

has_active :- 
    fact_exists('com.nokia.policy.call', [state], [active]).

has_active_cellular_call :-
    fact_exists('com.nokia.policy.call', [state, path], [active, Path]),
    is_cellular_path(Path).

has_active_ip_call :-
    fact_exists('com.nokia.policy.call', [state, path], [active, Path]),
    \+ is_cellular_path(Path).


/* basic list manipulation */
head([H|_], H).
tail([_|T], T).




/*
 * fake facts for testing
 */

/*
fact(10, 'org/freedesktop/Telepathy/Connection/ring/tel/ring/Channel0',
	active, incoming).
fact(11, '/a/pesky/skype/call/Channel2' , onhold,  outgoing).
fact(12, '/s/sip/call/by/sofia/Channel0', created, incoming). 

fact_exists(_, [id, state], [Id, State]) :- fact(Id, _, State, _).
fact_exists(_, [id, path], [Id, Path]) :- fact(Id, Path, _, _).
fact_exists(_, [id, state, path], [Id, State, Path]) :-
    fact(_, Id, State, Path).
fact_exists(_, [state, path], [State, Path]) :- fact(_, Path, State, _).
fact_exists(_, [state, dir], [State, Dir]) :- fact(_, _, State, Dir).
fact_exists(_, [state], [State]) :- fact(_, _, State, _).
fact_exists(_, [path], [Path]) :- fact(_, Path, _, _).
*/