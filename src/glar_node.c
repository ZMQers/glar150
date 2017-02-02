/*  =========================================================================
    glar_node - Glar150 service

    Copyright (c) the Contributors as noted in the AUTHORS file.       
    This file is part of the Glar150 Project.                          
                                                                       
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.           
    =========================================================================
*/

/*
@header
    glar_node - Glar150 service
@discuss
@end
*/

#include "glar_classes.h"
#include "glar_node_fsm.h"      //  Generated state machine engine

//  Structure of our class

struct _glar_node_t {
    bool verbose;               //  Trace activity yes/no
    fsm_t *fsm;                 //  Our finite state machine
    zyre_t *zyre;               //  Zyre node instance
    zactor_t *console;          //  Command line input
    zactor_t *button;           //  Button monitor
    zpoller_t *poller;          //  Socket poller
    zmsg_t *msg;                //  Last message we received
    zyre_event_t *event;        //  Last zyre_event received
};

static int
s_handle_console (zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    char command [1024];
    if (fgets (command, sizeof (command), stdin)) {
        //  Discard final newline
        command [strlen (command) - 1] = 0;
        if (*command)
            zstr_send ((zsock_t *)arg, command);
    }

    return 0;
}

static int
s_handle_pipe (zloop_t *loop, zsock_t *reader, void *arg)
{
    int terminated = 0;
    zmsg_t *request = zmsg_recv (reader);
    if (!request)
        return 0;        //  Interrupted

    char *command = zmsg_popstr (request);
    if (streq (command, "$TERM"))
        terminated = -1;
    zstr_free (&command);
    zmsg_destroy (&request);

    return terminated;
}

static void
s_console_actor (zsock_t *pipe, void *args)
{
    zmq_pollitem_t item;
    zloop_t *loop = zloop_new();
    assert (loop);
    item.socket = NULL;
    item.fd = STDIN_FILENO;
    item.events = ZMQ_POLLIN;
    item.revents = 0;
    int rc = zloop_poller (loop, &item, s_handle_console, pipe);
    assert (rc == 0);
    rc = zloop_reader (loop, pipe, s_handle_pipe, NULL);
    assert (rc == 0);


    zsock_signal (pipe, 0);             //  Tell caller we're ready
    zloop_start(loop);
    zloop_destroy (&loop);
}


static void
s_button_actor (zsock_t *pipe, void *args)
{
    zsock_signal (pipe, 0);             //  Tell caller we're ready
    int last_value = 0;                 //  Assume button is off
    zpoller_t *poller = zpoller_new (pipe, NULL);

    while (!zsys_interrupted) {
        if (zpoller_wait (poller, 250))
            break;                      //  Caller told us to terminate

        int handle = open ("/sys/class/gpio/gpio8/value", O_RDONLY);
        if (handle != -1) {
            char value [2] = { 0, 0 };
            if (read (handle, value, 1) == 1
            &&  last_value != value [0] - '0') {
                last_value = value [0] - '0';
                zstr_send (pipe, value);
            }
            close (handle);
        }
    }
    zpoller_destroy (&poller);
}


//  --------------------------------------------------------------------------
//  Create a new glar node

glar_node_t *
glar_node_new (const char *iface, bool console)
{
    glar_node_t *self = (glar_node_t *) zmalloc (sizeof (glar_node_t));
    assert (self);
    self->fsm = fsm_new (self);

    //  Grab us a new Zyre node
    self->zyre = zyre_new (NULL);
    zyre_set_interface (self->zyre, iface);
    zsys_info ("using interface=%s my_uuid=%s my_name=%s", iface,
            zyre_uuid (self->zyre),
            zyre_name (self->zyre));

    //  Start actors
    self->button = zactor_new (s_button_actor, NULL);
    self->poller = zpoller_new (
        zyre_socket (self->zyre), self->button, NULL);

    if (console) {
        self->console = zactor_new (s_console_actor, NULL);
        zpoller_add (self->poller, self->console);
    }
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the glar node

void
glar_node_destroy (glar_node_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        glar_node_t *self = *self_p;
        fsm_destroy (&self->fsm);
        zyre_destroy (&self->zyre);
        zactor_destroy (&self->console);
        zactor_destroy (&self->button);
        zpoller_destroy (&self->poller);
        zmsg_destroy (&self->msg);
        zyre_event_destroy (&self->event);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Set verbose on/off

void
glar_node_set_verbose (glar_node_t *self, bool verbose)
{
    assert (self);
    self->verbose = verbose;
    if (verbose) {
        zyre_set_verbose (self->zyre);
        fsm_set_animate (self->fsm, true);
    }
}


//  --------------------------------------------------------------------------
//  Execute state machine until finished

void
glar_node_execute (glar_node_t *self)
{
    assert (self);
    fsm_set_next_event (self->fsm, self->console? console_event: robot_event);
    fsm_execute (self->fsm);
}


//  ---------------------------------------------------------------------------
//  join_network_as_robot
//

static void
join_network_as_robot (glar_node_t *self)
{
    zyre_set_header (self->zyre, "ROLE", "%s", "robot");
    zyre_start (self->zyre);
    zyre_join (self->zyre, "GLAR");
    //  Show rotating sequence until peer joins
}


//  ---------------------------------------------------------------------------
//  join_network_as_console
//

static void
join_network_as_console (glar_node_t *self)
{
    zyre_set_header (self->zyre, "ROLE", "%s", "console");
    zyre_start (self->zyre);
    zyre_join (self->zyre, "GLAR");
}


//  ---------------------------------------------------------------------------
//  wait_for_activity
//

static void
s_handle_activity (glar_node_t *self, void *which)
{
    zmsg_destroy (&self->msg);
    if (which == (void *) zyre_socket (self->zyre)) {
        zyre_event_destroy (&self->event);
        self->event = zyre_event_new (self->zyre);
        self->msg = zyre_event_get_msg (self->event);

        if (streq (zyre_event_type (self->event), "JOIN"))
            fsm_set_next_event (self->fsm, join_event);
        else
        if (streq (zyre_event_type (self->event), "LEAVE"))
            fsm_set_next_event (self->fsm, leave_event);
        else
        if (streq (zyre_event_type (self->event), "WHISPER"))
            fsm_set_next_event (self->fsm, whisper_event);
        else
        if (streq (zyre_event_type (self->event), "SHOUT"))
            fsm_set_next_event (self->fsm, shout_event);
        else
            fsm_set_next_event (self->fsm, other_event);
    }
    else
    if (which == (void *) self->button) {
        char *status = zstr_recv (self->button);
        if (streq (status, "0"))
            fsm_set_next_event (self->fsm, button_on_event);
        else
        if (streq (status, "1"))
            fsm_set_next_event (self->fsm, button_off_event);
        else
            zsys_error ("Bad button status: %s", status);
        free (status);
    }
    else
    if (which == (void *) self->console) {
        self->msg = zmsg_recv (self->console);
        fsm_set_next_event (self->fsm, console_command_event);
    }
}

static void
wait_for_activity (glar_node_t *self)
{
    void *which = zpoller_wait (self->poller, -1);
    if (which)
        s_handle_activity (self, which);
    //  Else state machine will end now
}


//  ---------------------------------------------------------------------------
//  check_for_activity
//

static void
check_for_activity (glar_node_t *self)
{
    void *which = zpoller_wait (self->poller, 1000);
    if (which)
        s_handle_activity (self, which);
    else
        fsm_set_next_event (self->fsm, nothing_event);
}


//  ---------------------------------------------------------------------------
//  shout_command_to_robots
//

static void
shout_command_to_robots (glar_node_t *self)
{
    zyre_shout (self->zyre, "GLAR", &self->msg);
}


//  ---------------------------------------------------------------------------
//  execute_the_command
//

static char *
s_run (const char *command)
{
    zchunk_t *chunk = zchunk_new (NULL, 0);
    FILE *stream = popen (command, "r");
    if (!stream)
        return NULL;
    char cur_line [1024];
    while (fgets (cur_line, sizeof (cur_line), stream))
        zchunk_extend (chunk, cur_line, strlen (cur_line));
    pclose (stream);
    char *results = zchunk_strdup (chunk);
    zchunk_destroy (&chunk);
    return results;
}


static void
execute_the_command (glar_node_t *self)
{
    char *command = zmsg_popstr (self->msg);
    zsys_info ("Run command '%s'", command);
    if (*command == '#')  {       //  was: Display command as Morse code
    }
    else {
        char *results = s_run (command);
        if (results) {
            zsys_info ("System '%s' OK", command);
            //  Flash LED 1 once slowly
            zyre_whispers (self->zyre, zyre_event_peer_uuid (self->event), "%s", results);
            free (results);
        }
        else {
            zsys_info ("System '%s' FAIL", command);
            zyre_whispers (self->zyre, zyre_event_peer_uuid (self->event), "%s", "failed");
        }
    }
    free (command);
}


//  ---------------------------------------------------------------------------
//  print_command_results
//

static void
print_command_results (glar_node_t *self)
{
    char *results = zmsg_popstr (self->msg);
    printf ("%s: %s\n", zyre_event_peer_name (self->event), results);
    free (results);
}


//  ---------------------------------------------------------------------------
//  signal_peer_joined
//

static void
signal_peer_joined (glar_node_t *self)
{
    if (self->console)
        zsys_info ("JOINED peer=%s", zyre_event_peer_name (self->event));
}


//  ---------------------------------------------------------------------------
//  signal_peer_left
//

static void
signal_peer_left (glar_node_t *self)
{
    if (self->console)
        zsys_info ("LEFT peer=%s", zyre_event_peer_name (self->event));
}



//  ---------------------------------------------------------------------------
//  show_at_rest_sequence
//

static void
show_at_rest_sequence (glar_node_t *self)
{
}


//  ---------------------------------------------------------------------------
//  start_emergency_sequence
//

static void
start_emergency_sequence (glar_node_t *self)
{
}


//  ---------------------------------------------------------------------------
//  stop_emergency_sequence
//

static void
stop_emergency_sequence (glar_node_t *self)
{
}


//  ---------------------------------------------------------------------------
//  leave_network
//

static void
leave_network (glar_node_t *self)
{
    zyre_leave (self->zyre, "GLAR");
    zyre_stop (self->zyre);
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
glar_node_test (bool verbose)
{
    printf (" * glar_node: ");

    //  @selftest
    glar_node_t *node = glar_node_new ("wlan0", false);
    if (verbose)
        glar_node_set_verbose (node, verbose);
    assert (node);
    glar_node_destroy (&node);
    //  @end
    printf ("OK\n");
}

