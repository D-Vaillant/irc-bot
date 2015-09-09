/*
 * =====================================================================================
 *
 *       Filename:  mybot.c
 *
 *    Description:  An implementation of an IRC Bot in C.
 *
 *        Version:  1.0
 *        Created:  08/31/2015 05:55:09 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  David Vaillant
 *         Credit:  dav7
 *
 * =====================================================================================
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>

#define NICK "Cendenbot"
#define CHANNEL "#thomsonslantern"
#define NETWORK "irc.freenode.net"

#define MAX_SAVED_TELLS 10
#define MAX_USERS 10

typedef struct tell {
    char *message;
    char *sender;
} tell;

typedef struct tellStack {
    tell history[MAX_SAVED_TELLS];
    char *recipient;
    char *where;
    int length;
} tellStack;

typedef struct tellStruct {
    tellStack list[MAX_USERS];
    char *users[MAX_USERS];
    char *wheres[MAX_USERS];
    int length;
} tellStruct;

int conn;
char sbuf[512];

static tellStruct tellRecord = 
    {
        /*.list = { { .history   = { { .message = "", .sender = "" } },
                    .recipient = "",
                    .where     = "",
                    .length    = 0 } }, */
        .users = { NULL },
        .wheres = { NULL },
        .length = 0
    };


/*-----------------------------------------------------------------------------
 * ERROR MESSAGE CODES 
 *    1: asprintf failure
 *          Generally failure to allocate memory correctly.
 *    2: tellRecord overflow
 *          Attempting to add tellStacks past the maximum amount specified
 *          by MAX_USERS. 
 *    3: tellStack overflow
 *          Attempting to add tells past the maximum amount specified by 
 *          MAX_SAVED_TELLS.
 *   -2: tellRecord underflow
 *          Attempting to access tellStacks past the minimum index specified
 *          by tellRecord.length.
 *-----------------------------------------------------------------------------*/

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tell_kill
 *  Description:  Frees up all the memory in a tell.
 * =====================================================================================
 */
void tell_kill(tell *victim) {
    if (victim == NULL) return;

    if (victim->message) {
        free(victim->message);
        victim->message = NULL;
    }

    if (victim->sender) {
        free(victim->sender);
        victim->sender = NULL;
    }
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tellStack_kill
 *  Description:  Frees up all the memory in a tellStack.
 * =====================================================================================
 */
void tellStack_kill(tellStack *victim) {
    if (victim == NULL) return;

    if (victim->recipient) {
        free(victim->recipient);
        victim->recipient = NULL;
    }

    if (victim->where) {
        free(victim->where);
        victim->where = NULL;
    }
    
    int n;
    for (n = 0; n < victim->length; n++) tell_kill(&(victim->history[n]));

    victim->length = 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tell_copy
 *  Description:  Transfers data from tell *in to tell *out.
 * =====================================================================================
 */
int tell_copy(tell *out, tell *in) {
    tell_kill(out);

    if(asprintf(&out->message, in->message) < 0) return 1; 
    if(asprintf(&out->sender, in->sender) < 0) return 1;

    return 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tellStack_copy
 *  Description:  Transfers data from tellStack *in to tellStack *out.
 * =====================================================================================
 */
int tellStack_copy(tellStack *out, tellStack *in) {
    tellStack_kill(out);

    out->length = in->length;
    if(asprintf(&out->recipient, in->recipient) < 0) return 1;
    if(asprintf(&out->where, in->where) < 0) return 1;

    int i;
    for (i = 0; i < in->length; i++) {
        tell_copy(&out->history[i], &in->history[i]);
    }

    return 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tellRecord_kill_stack
 *  Description:  Frees up all memory at tellRecord.list[index].
 *     Warnings:  Doesn't prevent against accessing the memory!
 * =====================================================================================
 */
void tellRecord_kill_stack(int index) {
    if (index < tellRecord.length) {
        tellStack_kill(&tellRecord.list[index]);

        if(tellRecord.users[index]) {
            free(tellRecord.users[index]);
            tellRecord.users[index] = NULL;
        }

        if(tellRecord.wheres[index]) {
            free(tellRecord.wheres[index]);
            tellRecord.wheres[index] = NULL;
        }

    } else {
        printf("Don't do that!\n");
    }
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tellRecord_remove_stack
 *  Description:  Given an index, removes the stack and user/where pair at that index
 *                and scoots all the other values down.
 * =====================================================================================
 */
int tellRecord_remove_stack(int index) {
    if (index >= tellRecord.length) return -2;

    /* EXAMPLE:
     *   A B X D E -
     *   A B - D E -
     *   A B D D E -
     *   A B D - E
     *   A B D E E -
     * And after the tellRecord_kill_stack call:
     *   A B D E - - */
    for (index; index < tellRecord.length-1; index++) {
        free(tellRecord.users[index]);
        if(asprintf(&tellRecord.users[index],
                    tellRecord.users[index+1]) < 0) return 1;

        free(tellRecord.wheres[index]);
        if(asprintf(&tellRecord.wheres[index],
                    tellRecord.wheres[index+1]) < 0) return 1;

        tellStack_copy(&tellRecord.list[index], &tellRecord.list[index+1]);
    }
    tellRecord_kill_stack(index);
    tellRecord.length--;

    return 0;
}


int is_channel(char *where) {
    return where[0] == '#' || where[0] == '&' ||
           where[0] == '+' || where[0] == '!';
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  raw
 *  Description:  Sends a formatted string to the network.
 * =====================================================================================
 */
void raw(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sbuf, 512, fmt, ap);
    va_end(ap);
    printf("<< %s", sbuf);
    write(conn, sbuf, strlen(sbuf));
}

/*-----------------------------------------------------------------------------
 *  Channel Methods
 *-----------------------------------------------------------------------------*/
void join(char *chan) {
    raw("JOIN %s\r\n", chan);
}

void change_nick(char *nick) {
    raw("NICK %s\r\n", nick);
}

void send_msg(char *target, char *msg) {
    raw("PRIVMSG %s :%s\n", target, msg);
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tell_push
 *  Description:  Creates a tell structure and adds it to a tellStack.
 *                Creates the necessary tellStack if it doesn't exist.
 * =====================================================================================
 */
int tell_push(char *where, char *recipient, char *sender, char *message) {
    int stack_exists = 0;
    int n;

    for(n = 0; n < tellRecord.length; n++) {
        if (!strcmp(tellRecord.users[n], recipient) &&
            !strcmp(tellRecord.wheres[n], where)) {
            printf("tellStack found.\n");
            stack_exists = 1;
            break;
        } /* checks if a tellStack corresponds to the given tell */ 
    }

    if (tellRecord.length >= MAX_USERS) return 2;
    tellStack *stack = &tellRecord.list[n];
    int len = tellRecord.length; /* because I like shorter lines */

    if (stack_exists) {
        printf("Grabbed the pointer to the tellStack.\n");
        if(stack->length >= MAX_SAVED_TELLS) return 3; 
    } else {
        // Initializing the tellStack.
        stack->length = 0;

        /* copying recipient to the stack and to tellRecord.users */
        if (asprintf(&stack->recipient, recipient) < 0) return 1;
        if (asprintf(&tellRecord.users[len], recipient) < 0) return 1;

        /* mutatis mutandis the above for wheres */
        if (asprintf(&stack->where, where) < 0) return 1;
        if (asprintf(&tellRecord.wheres[len], where) < 0) return 1;

        printf("tellStack initialized.\n");

        tellRecord.length++;
    }

    /* grabbing the pointer to the next free tell location */
    tell *T = &((stack->history)[stack->length]);
    stack->length++;
    printf("tell #%d added to the stack.\n", stack->length);

    /* saving the message and sender to the tell */
    if (asprintf(&T->sender, sender) < 0) return 1;
    if (asprintf(&T->message, message) < 0) return 1;
    printf("Assignment to tell structure successful.\n");

    /* acknowledging saved tell */
    char *msgy;
    if(asprintf(&msgy, "%s: Message queued for %s.",
                sender, recipient) < 0) return 1;
    send_msg(where, msgy);
    return 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  should_tell_pop
 *  Description:  Takes a user and a target and checks if there's a non-empty tellStack
 *                that corresponds to them.
 * =====================================================================================
 */
int should_tell_pop(char *recipient, char *where) {
    int n;
    for(n = 0; n < tellRecord.length; n++) {
        if(!strcmp(tellRecord.users[n], recipient) &&
           !strcmp(tellRecord.wheres[n], where))
        {
            return n;
        }
    }
    return -1;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tell_pop
 *  Description:  Given an index, pops a tell from the corresponding tellStack located
 *                in a tellStruct.
 * =====================================================================================
 */
int tell_pop(int index) {
    if(index >= tellRecord.length) return -2; 

    tellStack *stack = &tellRecord.list[index];
    tell *retrieved = &stack->history[stack->length-1];

    char *message;
    if(asprintf(&message, "%s: %s said %s", stack->recipient,
                retrieved->sender, retrieved->message) < 0) return 1;

    char *where = is_channel(stack->where) ? stack->where : stack->recipient;
    send_msg(where, message);

    tell_kill(retrieved);
    if(stack->length == 1) {
        tellRecord_remove_stack(index); 
    } else { stack->length--; }
    return 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  core
 *  Description:  Takes processed input and redirects it to the appropriate functions.
 * =====================================================================================
 */
int core(char *user, char *command, char *where, char *target, char *message)
{
    // implements responding to tells
    int q = should_tell_pop(user, where);
    if (q >= 0) tell_pop(q);

    // implements .tell
    // USAGE: .tell NICK MESSAGE
    // Next time NICK sends a PRIVMSG, bot will notify NICK of the message.
    // Uses *where to determine where to send the PRIVMSG.
    if(!strncmp(message, ".tell ", 5)) {
        printf("Calling TELL function.\n");
        int s = 6;
        while(message[s] != ' ' && message[s] != '\0') {
            s++;
        }
        if(message[s] == '\0') return 0;
        else {
            message[s] = '\0';
            char *outmsg;
            switch(tell_push(where, &message[6], user, &message[s+1])) {
                case 1: 
                    if(asprintf(
                            &outmsg,
                            "%s: Sorry, memory problems. Please try again.",
                             user) < 0) return 1;

                    send_msg(where, outmsg);
                    break;
                case 5:
                    if(asprintf(
                            &outmsg,
                            "%s: Sorry, there are too many messages queued.",
                            user) < 0) return 1;

                    send_msg(where, outmsg); 
                    break;
                case 6:
                    if(asprintf(
                            &outmsg,
                            "%s: Sorry, %s has too many messages queued.",
                            user, &message[6]) < 0) return 1;

                    send_msg(where, outmsg); 
                    break;
                default:
                    if(asprintf(&outmsg, "%s: Message queued for %s.",
                                user, &message[6]) < 0) return 1;

                    break;
            }
            free(outmsg);
        }
    }
    return 0;
}

int main() {
    char *nick = NICK;
    int nick_count = 0;
    char *channel = CHANNEL;
    char *host = NETWORK;
    char *port = "6667";

    char *user, *command, *where, *message, *sep, *target;
    int i, j, l, sl, o = -1, start, wordcount;
    char buf[513];
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(host, port, &hints, &res);
    conn = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(conn, res->ai_addr, res->ai_addrlen);

    // identification!
    raw("USER %s 0 * :%s\r\n", nick, nick);
    change_nick(nick);

    // read the connection buffer
    while ((sl = read(conn, sbuf, 512))) {
        for (i = 0; i < sl; i++) {
            o++;
            // copy connection buffer into buf
            buf[o] = sbuf[i];

            // if we've reached the end of the connection buffer:
            if ((i > 0 && sbuf[i] == '\n' && sbuf[i - 1] == '\r') || o == 512) {
                // string terminate buf
                buf[o + 1] = '\0';
                // set l to the last char index
                l = o;
                // reset o
                o = -1;

                // print server message to the console
                printf(">> %s", buf);

                // changes PING to PONG if buf is [P,I,N,G,...]
                if (!strncmp(buf, "PING", 4)) {
                    buf[1] = 'O';
                    raw(buf);
                } else if (buf[0] == ':') {
                    wordcount = 0;
                    user = command = where = message = NULL;
                    // processes the input into user,command,where
                    for (j = 1; j < l; j++) {
                        if (buf[j] == ' ') {
                            // replace space with string terminator
                            buf[j] = '\0';
                            // note how many words so far
                            wordcount++;

                            switch(wordcount) {
                                case 1: user = buf + 1; break;
                                case 2: command = buf + start; break;
                                case 3: where = buf + start; break;
                            }
                            // break out if we're at the end of buf
                            if (j == l - 1) continue;
                            // set the beginning of next word
                            start = j + 1;
                        /* if we hit : and we have user, command, and where,
                              we've reached the message. */
                        } else if (buf[j] == ':' && wordcount == 3) {
                            // making sure that we don't overflow here
                            if (j < l - 1) message = buf + j + 1;
                            break;
                        } /* if we reach a space */
                    } /* for each character in message */

                    // break out if we don't have a command
                    if (wordcount < 2) continue;

                    if (!strncmp(command, "001", 3) && channel != NULL) {
                        join(channel); 

                    } else if (!strncmp(command, "433", 3)) {
                        //int x = strlen(nick); 
                        char *new_nick;
                        if (!nick_count) {
                            new_nick = "Cendenbot_";
                            nick_count++;
                        } else {
                            new_nick = "Cenderbot"; // Do something here.
                            nick_count--;
                        }
                        change_nick(new_nick);

                    } else if (!strncmp(command, "PRIVMSG", 7) || 
                               !strncmp(command, "NOTICE", 6)) {
                        // break out if no message or location
                        if (where == NULL || message == NULL) continue;
                        if ((sep = strchr(user, '!')) != NULL) {
                            // cuts the nick from the IP stuff
                            user[sep - user] = '\0'; 
                        }
                        
                        target = (is_channel(where)) ? where : user;

                        // beginning actual processing
                        printf("[from: %s] [reply-with: %s] [where: %s]"
                               "[reply-to: %s] %s", user, command, where, 
                                                    target, message);
                        
                        int error_code = core(user, command, where,
                                              target, message);
                        switch (error_code) {
                            case 1:
                                send_msg(where, "Sorry, something went wrong.");
                                break;
                            default:
                                break;
                        }
                    } /* if command is a PRIVMSG or a NOTICE */
                } /* if msg begins with a : */
            } /* if we're reached the end of sbuf */
        } /* for each integer less than the length of sbuf */
    } /* while the length of sbuf is non-zero */
    return 0;
}
