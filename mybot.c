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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>

#define MAX_SAVED_TELLS 10
#define MAX_USERS 10
#define MAX_USERNAME_LENGTH 30

typedef struct {
    char message[512];
    char recipient[MAX_USERNAME_LENGTH];
    char where[30];
} tell;

typedef struct {
    tell history[MAX_SAVED_TELLS];
    char recipient[MAX_USERNAME_LENGTH];
    int length;
} tellStack;

typedef struct {
    tellStack list[MAX_USERS];
    char users[MAX_USERS][MAX_USERNAME_LENGTH];
    int length;
} tellStruct;

int conn;
char sbuf[512];

static tellStack tellHistory[MAX_USERS];
static char userWatchList[MAX_USERS][MAX_USERNAME_LENGTH] = { 0 };
static tellStruct tellRecord = 
    {
        .list = { { .history = { { .message = "", .recipient = "", .where = "" } },
                    .recipient = "",
                    .length = 0 } },
        .users = {""},
        .length = 0
    };

int protected_strcpy(char *destination, char *source) {
    /*
    if(strlen(destination) < strlen(source)) {
        return 1;
    } else {*/
    strcpy(destination, source);
    return 0;
}

int is_channel(char *where) {
    return where[0] == '#' || where[0] == '&' ||
           where[0] == '+' || where[0] == '!';
}

void raw(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sbuf, 512, fmt, ap);
    va_end(ap);
    printf("<< %s", sbuf);
    write(conn, sbuf, strlen(sbuf));
}

void join(char *chan) {
    raw("JOIN %s\r\n", chan);
}

void send_msg(char *target, char *msg) {
    raw("PRIVMSG %s :%s\n", target, msg);
}


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  tell_push
 *  Description:  Creates a tell structure and adds it to a tell stack.
 *                Creates the necessary tell stack if it doesn't exist.
 *   Error MSGS:  1: Buffer overflow in copying where, recipient, or message.
 *                5: Buffer overflow in tellStruct (too many tellStacks).
 *                6: Buffer overflow in tellStack (too many tells).
 * =====================================================================================
 */
int tell_push(char *where, char *recipient, char *message) {
    tell *T = malloc(sizeof(tell));
    if(protected_strcpy(T->where, where)) return 1;
    if(protected_strcpy(T->recipient, recipient)) return 1;
    if(protected_strcpy(T->message, message)) return 1;
    printf("Assignment to tell structure successful.\n");

    int stack_exists = 0;
    int n;
    for(n = 0; n < tellRecord.length; n++) {
        if(!strcmp(tellRecord.users[n], T->recipient)) {
            printf("tellStack found.\n");
            stack_exists = 1;
            break;
        } 
    }

    tellStack *stack;
    if(stack_exists) {
        printf("Grabbing the pointer to the tellStack.\n");
        stack = &tellRecord.list[n];
    } else {
        // Basic overflow checking for tellStruct.
        if(tellRecord.length < MAX_USERS) {
            // Initializing the tellStack.
            stack = malloc(sizeof(tellStack));
            stack->length = 0;
            if(protected_strcpy(stack->recipient, recipient)) return 1;
            printf("tellStack initialized.\n");

            // Adding the tellStack to tellStruct.
            (tellRecord.list)[stack->length] = *stack;
            protected_strcpy((tellRecord.users)[stack->length], T->recipient);
            ++tellRecord.length;
        } else {
            return 5;
        }
    }

    if(stack->length < MAX_SAVED_TELLS) {
        (stack->history)[stack->length] = *T;
        printf("%dth tell added to the stack.", stack->history);
        stack->length++;
    } else { return 6; }

    return 0;
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  core
 *  Description:  Takes processed input and redirects it to the appropriate functions.
 * =====================================================================================
 */
void core(char *user, char *command, char *where, char *target, char *message)
{
    // responds to PMs
    if(!is_channel(where)) strcpy(user, where);

    // implements !tell
    // USAGE: !tell NICK MESSAGE
    // Next time NICK sends a PRIVMSG, bot will notify NICK of the message.
    // Uses *where to determine where to send the PRIVMSG.
    if(!strncmp(message, "!tell ", 5)) {
        printf("Calling TELL function.\n");
        int s = 6;
        while(message[s] != ' ' && message[s] != '\0') {
            s++;
        }
        if(message[s] == '\0') return;
        else {
            message[s] = '\0';
            char *outmsg;
            switch(tell_push(where, &message[6], &message[s+1])) {
                case 1: 
                    asprintf(&outmsg, "%s: Sorry, something went wrong.", user);
                    send_msg(where, outmsg);
                    break;
                case 5:
                    asprintf(&outmsg, "%s: Sorry, there are too many messages queued.",
                                     user);
                    send_msg(where, outmsg); 
                    break;
                case 6:
                    asprintf(&outmsg, "%s: Sorry, %s has too many messages queued.", 
                                    user, &message[6]);
                    send_msg(where, outmsg); 
                    break;
                default:
                    asprintf(&outmsg, "%s: Message queued for %s.", user, &message[6]);
                    break;
            }
            free(outmsg);
        }
    }
}

int main() {
    char *nick = "Cendenbot";
    //char *channel = "#thomsonslantern";
    char *channel = "#CendenbotHaus";
    char *host = "irc.freenode.net";
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
    raw("NICK %s\r\n", nick);

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
                        // if we hit : and we have user, command, and where,
                        // we've reached the message.
                        } else if (buf[j] == ':' && wordcount == 3) {
                            // making sure that we don't overflow here
                            if (j < l - 1) message = buf + j + 1;
                            break;
                        }
                    }

                    // break out if we don't have a command
                    if (wordcount < 2) continue;

                    if (!strncmp(command, "001", 3) && channel != NULL) {
                        join(channel); 
                    } else if (!strncmp(command, "PRIVMSG", 7) || 
                               !strncmp(command, "NOTICE", 6)) {
                        // break out if no message or location
                        if (where == NULL || message == NULL) continue;
                        if ((sep = strchr(user, '!')) != NULL) {
                            // cuts the nick from the IP stuff
                            user[sep - user] = '\0'; 
                        }
                        if (is_channel(where)) { 
                            target = where;
                        } else target = user; 
                        
                        // beginning actual processing
                        printf("[from: %s] [reply-with: %s] [where: %s]"
                               "[reply-to: %s] %s", user, command, where, 
                                                    target, message);
                        

                        core(user, command, where, target, message);
                    }
                }
            }
        }
    }
    return 0;
}
