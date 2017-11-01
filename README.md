# Glar150

## Background

We bought a bunch of [GL-AR150 OpenWRT routers](http://www.gl-inet.com/ar150/) off Amazon.com, and wondered what fun we could have with these. These are tiny, cheap WiFi servers ($25). They look cool, with an antenna that makes me think of my old metal-body Ericsson R520m. Now that was a phone you could crack skulls with!

## Exploring the Hardware

Unlike the Ericssons of yore, the GL-AR150 (which I will call the "Glar" from now on, or "Glar 150" if you want to be specific) has no on/off switch. You provide power, it comes on. You remove power, it goes off. This works especially well with modern power packs that have auto power-on. About half the power packs we tested have "auto power-on and then random power-off when no-one's looking", so be warned. The Glar has a reset switch you can hold and press to reset your router configuration. It also has a rocker switch, status lights, a USB port (controllable power via GPIO), an external antenna in the version we chose, and two RJ45 sockets. All in a nice simple case. Nice!

### Seeing Red

Our first experiment was to run [Malamute](https://github.com/zeromq/malamute), a tiny message broker. This worked nicely. Building for OpenWRT takes a little cross-compiling black magic, yet our ZeroMQ software stack runs perfectly on this box, as it should.

Next we wondered, could we attach large LEDs to those I/O pins? A real computer has flashing lights. Turns out the red rear lights from my kids' bikes take 3V, and a quick test showed the Glar could power these easily. Luckily my kids weren't paying attention, so I nabbed the lamps and we opened them up.

These bike lamps cost EUR 1.39 each, so we didn't feel bad about cutting out the on/off button and soldering wires directly onto the PCB. Let me explain briefly how we power and control the lamp. We have two wires, one feeds the PCB with power. The other takes the used electrons from the PCB and sends them to the recycle bin we call "ground." The Glar opens real easy, just pull off the bottom cover and then ease out the PCB.

Most of the Glar's already tiny volume is used up by legacy connectors. If we didn't have those two network plugs and the USB slot, it would fit into a case a quarter of the size. The Glar has 16MB flash and 64MB RAM, and is powered by a [32-bit MIPS core](http://www.gl-inet.com/ar-specifications/). It's not meant for heavy duty. The CPU technology is a decade old. It is cheap and low power.

Let me quantify this. When we tested Malamute, we compared a Glar to a modern laptop. The workload is a mix of network I/O, memory copying, and computation. We tested on Ethernet, since it is so easy to saturate WiFi that it's useless for comparisons. The laptop can push around 200,000 messages per second between publishers and subscribers. The Glar can do around 5,000 per second.

As for power consumption, a laptop draws 10-20 Watts, whereas the Glar draws under 1 Watt. What this means in practice is that the Glar will run for 12 hours off a small LiPo battery pack. I opened up one of our little battery packs. Inside there's what looks like a camera battery. This would fit neatly inside a Glar case, methinks. Aha, looks like GLI are developing [a battery-powered version](http://www.gl-inet.com/m9331-mifi/).

Back to the red light. Here's a shell script that switches it on and then off:

```
echo 1 > /sys/class/gpio/gpio1/value
sleep 1
echo 0 > /sys/class/gpio/gpio1/value
```

We need to prepare the GPIO at startup:

```
echo 1 > /sys/class/gpio/gpio1/export
echo out > /sys/class/gpio/gpio1/direction
```

Controlling the lamp from C is then easy:

```
char *value = "1";      //  Or "0" to switch off
int handle = open ("/sys/class/gpio/gpio1/value", O_WRONLY);
if (handle != -1) {
    if (write (handle, value, strlen (value)) == -1)
        printf ("can't write to GPIO 1\n");
    close (handle);
}
```

### Blinken Lichten

The Glar also has three status LEDs. You can play with these using GPIO (they are GPIO 13, 15, and 0 (aka 16)). There is a preloaded kernel module that controls them, so we don't use the raw GPIO interface. Instead, we speak to this module by writing to a sysfs system device:

```
/sys/devices/platform/leds-gpio/leds/gl_ar150:wan/brightness
```

From left to right LEDs are called "wan", "lan", and "wlan." The wlan LED is red, the other two are green. So again, from C they are easy to control:

```
char *value = "1";      //  Or "0" to switch off
int handle = open ("/sys/devices/platform/leds-gpio/leds/gl_ar150:wan/brightness", O_WRONLY);
if (handle != -1) {
    if (write (handle, value, strlen (value)) == -1)
        zsys_error ("can't write to LED device");
    close (handle);
}
```

It is possible (we think) to unload the LED module, and then control these lights through the GPIO interface, as we did for GPIO1. This would make the code more consistent. It's not worth the effort though. Note that the Glar has 16 GPIOs of which eight are standard 2.54mm (0.1") physical pins. The other eight are wired to various bits and pieces like the rocker switch.

### The Rocker Switch

The rocker switch actually has three positions on the PCB, which was confusing enough that the designers made space for just two positions on the case. You can drill out the hole to get a third position if you absolutely love the tertiary counting system. Anyhow, reading the switch is easy, it's GPIO 8:

```
int handle = open ("/sys/class/gpio/gpio8/value", O_RDONLY);
if (handle != -1) {
    char value [2] = { 0, 0 };
    if (read (handle, value, 1) == 1)
        if (*value == '1')
            //  Switch is off
        else
            //  Switch is on
    }
    close (handle);
}
```

Note that a value of "1" means the switch is at the 'Off' position. If you use the third position, you will want to read GPIO 7 as well.

### Other Possibilities

We can get 3V and perhaps 50mA from the GPIO. It's enough to drive those LED bike lamps. For more demanding applications the Glar lets you control the USB power via GPIO 6. You could also connect an external power relay to switch your heating on and off remotely, for example.

The reset button uses GPIO 11 and you could (I assume, we did not test) use this as a control switch. It's probably not the thing you want to interfere with.

Finally, of course, the Glar is a network animal and this is where its real power comes to play.

## Building a Glar Cluster

What's the fastest, simplest way to connect a bunch of Glars together in a cluster?

I'd say (totally biased, since I wrote this project), use [Zyre](http://zyre.com). That's a clustering library built exactly for this purpose: a bunch of things on WiFi want to talk to each other. Zyre uses ZeroMQ and is written in C. There are Python and Java versions.

It is distressingly easy to use Zyre in your code:

```
zyre_node_t *zyre = zyre_new (NULL);
zyre_set_interface (zyre, "wlan0");
zyre_start (zyre);
zyre_join (zyre, "GLAR");
```

This kicks off a Zyre node and joins the GLAR group so it can chat to other nodes in the same group.

Building Zyre for OpenWRT is a little more challenging. I don't actually know how this works, since my colleague Zoobab does this. He takes libzmq, CZMQ, Zyre, and the Glar150 code, waves his hands, and magic happens. When he gets back from lunch with my sandwich (it's been about three days now and I'm starting to get a little worried, and also hungry), we'll write some explanations.

Anyhow, once Zyre is up and running it is trivial to send a message to all nodes:

```
zyre_shouts (zyre, "GLAR", "%s", "Hello, World");
```

## Let's Botnet

I had this vision of throwing code at a network of devices. It's a lot like the Web (device = browser, code = webpage + JavaScript). It's a lot like botnets (device = your family PC, code = evil stuff).

So our first idea was to run a JavaScript engine on the Glars, and shout JavaScript at them using Zyre. *Bad idea.* JavaScript is a fine, fine language. It's just not yet ready for the embedded world. There are small Javascript engines like JerryScript. They do not build and run on the MIPS processors that many OpenWRT routers use.

After a few days of failing to get anything remotely like JavaScript running on a Glar, we wondered. What's the simplest possible language for remote execution? I mean, these things all run root, so what's the *worst* that can happen, right?

How about shell scripts?

Ah, so imagine my laptop, connected to the Glar/Zyre network, doing this:

    zyre_shout (zyre, "GLAR", "df");

Where each node runs that command, grabs the results (`man popen` is your friend) and sends it back to the controller laptop. Turns out that works nicely. Secure? Yes, as we're on a private, secure WiFi network.

Eventually I'd like a proper remote control language. Something compact, safe, and threaded. In the meantime we might use more shell. Yet for now, we were distracted by the flashing lights.

## More Threads

Working with lights and buttons immediately gave us a challenge. How do we deal with many devices, often with timing dependent operations like "wait for 200msec before turning off again" from a single thread?

I mean, it's possible to do a lot of async work in a single thread. You need to set up timers and a lot of callbacks and `select()` or `epoll()` calls for network I/O. There are entire libraries that do this, like `libuv` and `libzmq`. I'm rolling with `libzmq` as it's kind of the Swiss Army Tank of lots of stuff happening at once, all over the place.

The old `libzmq` library API is as friendly as a squad of soldiers in unmarked uniforms brandishing shiny AK-74Ms. It does the job, yet you don't really want to be there when it happens. A much nicer API is [CZMQ](http://czmq.zeromq.org), which hides the cold brutality that is high-performance messaging under a cloak of charm.

## The World is Our Stage

One thing CZMQ gives us, which turns out to be really useful in our demo, are *actors*. An actor is a thread that you talk to over a ZeroMQ socket. It is not a complete actor system like Erlang or Akka actors. Yet it is a really neat way of dealing with concurrency.

Let me give one example of where we need an actor. When we run as a console, we talk to a number of Glars (aka the "Robot Army"). Our UI is simple: take one command line from the user, blast it at the robot army. The problem is that `fgets` is blocking. What we need is a non-blocking `fgets` that waits for input, and lets us talk to the robot army at the same time.

So for instance, while the user is happily typing `rm -rf /*` the console can be getting events like JOIN and LEAVE from other Zyre nodes.

Here is what [the actual console actor](https://github.com/CodeJockey/glar150/blob/master/src/glar_node.c#L38) looks like:

```
static void
s_console_actor (zsock_t *pipe, void *args)
{
    zsock_signal (pipe, 0);             //  Tell caller we're ready
    while (!zsys_interrupted) {
        char command [1024];
        if (fgets (command, sizeof (command), stdin)) {
            //  Discard final newline
            command [strlen (command) - 1] = 0;
            if (*command)
                zstr_send (pipe, command);
        }
    }
}
```

To start the actor we do this:

```
zactor_t *console = zactor_new (s_console_actor, NULL);
```

And to stop it, we do this:

```
zactor_destroy (&console);
```

We can read one command from it (a line of text the user typed) like this:

```
char *command = zstr_recv (console);
```

Though in practice we use a `zpoller_t` object to wait on several actors at once:

```
//  Create a set of actors and a poller
zactor_t *panel = zactor_new (panel_actor, NULL);
zactor_t *morse = zactor_new (morse_actor, NULL);
zactor_t *console = zactor_new (console_actor, NULL);
zpoller_t *poller = zpoller_new (panel, morse, console, NULL);

//  Deal with messages from actors, until we get Ctrl-C
while (!zsys_interrupted) {
    zactor_t *which = (zactor_t *) zpoller_wait (poller, -1);
    if (which)
        break;      //  Nothing, so poll was interrupted
    zmsg_t *msg = zmsg_recv (which);
    ...
}
//  Destroy our objects
zpoller_destroy (&poller);
zactor_destroy (&panel);
zactor_destroy (&morse);
zactor_destroy (&console);
```

### Reading The Button State

Let me show some an actor that does GPIO. We'll look at the actor that reads the button state. What the code has to do is quite simple: check the button and tell the main thread when the button changes from off to on, or vice-versa.

There is no async I/O on the button so our code is brutal: wait for a short time, check the button state, see if it changed, and repeat until the process ends.

Here is the actor function for that:

```
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
```

### Morse Code Actor

A separate thread (an actor, [glar_morse](https://github.com/CodeJockey/glar150/blob/master/src/glar_morse.c)) turns commands into Morse code and blinks them on the large red lamp.

Using an actor here means we can send off a Morse sequence and let it run in the background, while doing other work.

## Finite State Machines

With our actor framework we have events like console input, Zyre events, and button change events coming into a single main thread. The main thread, however, has several states. We could be running as a console, or as a robot, and we could be in emergency state.

A nice and robust way to handle events in different states is to use a `finite state machine`. This is a logical model that specifies each state, and each event. Here is the full state machine for the main thread. We write it as XML, so that we can use the [GSL code generator](https://github.com/imatix/gsl) to turn it into C code:

```
<class name = "glar_node" script = "fsm_c">

<state name = "start">
    <event name = "console" next = "as console">
        <action name = "join network as console" />
        <action name = "wait for activity" />
    </event>
    <event name = "robot" next = "as robot">
        <action name = "join network as robot" />
        <action name = "wait for activity" />
    </event>
</state>

<state name = "as console" inherit = "defaults">
    <event name = "console command">
        <action name = "shout command to robots" />
        <action name = "wait for activity" />
    </event>
    <event name = "whisper">
        <action name = "print command results" />
        <action name = "wait for activity" />
    </event>
</state>

<state name = "as robot" inherit = "defaults">
    <event name = "shout">
        <action name = "execute the command" />
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "button on" next = "emergency">
        <action name = "start emergency sequence" />
        <action name = "check for activity" />
    </event>
</state>

<state name = "emergency" inherit = "defaults">
    <event name = "button off" next = "as robot">
        <action name = "stop emergency sequence" />
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "nothing">
        <action name = "check for activity" />
    </event>
    <event name = "whisper">
        <action name = "check for activity" />
    </event>
</state>

<state name = "defaults">
    <event name = "finished">
        <action name = "leave network" />
    </event>
    <event name = "join">
        <action name = "signal peer joined" />
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "leave">
        <action name = "signal peer left" />
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "other">
        <action name = "wait for activity" />
    </event>
    <event name = "button on">
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "button off">
        <action name = "show at rest sequence" />
        <action name = "wait for activity" />
    </event>
    <event name = "*">
        <action name = "wait for activity" />
    </event>
</state>

</class>
```

The [code generator](https://github.com/CodeJockey/glar150/blob/master/src/fsm_c.gsl) is a GSL script that takes the XML and issues C code, a long `case` statement in our case. There are other ways to make FSMs, especially using tables. This is the simplest, and fast enough.

## Building the Code

Still to be written.

## Run a demo with multipe docker containers

The glard daemon has been rewritten to drop the GPIO blinking features at last
ZMQ hackaton.

It can now be used to launch commands on multiple slaves.

A simple way to test the glard daemon is to use a docker container which spawns a glard daemon:

```
$ docker build -f Dockerfile.obs -t glard
```

You should then have an image named "glard:latest" in "docker images".

If you want to avoid building the docker image, there is one prebuilt for you:

```
$ docker run zoobab/glar150
glard v1.0.1 -- GL-AR150 demo'n
I: 17-11-01 18:08:26 using interface=eth0 my_uuid=1CF2281F0C9A4DD596C29EF1EF2F5C17 my_name=1CF228
```

Then spawn 3 containers running glard:

```
$ docker run -d glard
b6d495af00604457061bd13fce0867f097fccfe569c074c9869fb2c0c047bf4e
$ docker run -d glard
5af819f9d7123acc407aa6cefcb992ca2b59fc06a191b6dc4d5fd590cea95f44
$ docker run -d glard
cafe37cf9499d9f64f55b9154b84d93038912ae091033e2f09aecd97b71499cd
```

Then launch the fourth one with the controller "-c" and the interactive shell:

```
$ docker run -it glard -c
glard v1.0.1 -- GL-AR150 demo'n
I: 17-11-01 17:52:04 using interface=eth0 my_uuid=C3A0AA7977B943D68AD5B7E1A5B8AEB4 my_name=C3A0AA
I: 17-11-01 17:52:04 JOINED peer=5A793D
I: 17-11-01 17:52:04 JOINED peer=CB5DA2
I: 17-11-01 17:52:04 JOINED peer=9EA303
```
You should then type the command "pwd" and observer that it is launched on the
3 slaves:

```
pwd
9EA303: /

CB5DA2: /

5A793D: /
```

If you launch the "hostname" command, you should find the names of the 3
containers that were launched earlier:

```
hostname
5A793D: 5af819f9d712

9EA303: b6d495af0060

CB5DA2: cafe37cf9499
```
