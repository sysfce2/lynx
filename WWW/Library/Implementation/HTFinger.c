/*
 * $LynxId: HTFinger.c,v 1.32 2025/01/06 15:47:35 tom Exp $
 *
 *			FINGER ACCESS				HTFinger.c
 *			=============
 * Authors:
 *  ARB  Andrew Brooks
 *
 * History:
 *	21 Apr 94   First version (ARB, from HTNews.c by TBL)
 *	12 Mar 96   Made the URL and command buffering secure from
 *		     stack modifications, beautified the HTLoadFinger()
 *		     and response() functions, and added support for the
 *		     following URL formats for sending a "", "/w",
 *		     "username[@host]", or "/w username[@host]" command
 *		     to the server:
 *			finger://host
 *			finger://host/
 *			finger://host/%2fw
 *			finger://host/%2fw%20username[@host]
 *			finger://host/w/username[@host]
 *			finger://host/username[@host]
 *			finger://host/username[@host]/w
 *			finger://username@host
 *			finger://username@host/
 *			finger://username@host/w
 *	15 Mar 96   Added support for port 79 gtype 0 gopher URLs
 *		     relayed from HTLoadGopher. - FM
 */

#include <HTUtils.h>

#ifndef DISABLE_FINGER

#include <HTAlert.h>
#include <HTML.h>
#include <HTParse.h>
#include <HTFormat.h>
#include <HTTCP.h>
#include <HTString.h>
#include <HTFinger.h>

#include <LYUtils.h>
#include <LYLeaks.h>

#define FINGER_PORT 79		/* See rfc742 */
#define BIG 1024		/* Bug */

#define PUTC(c) (*targetClass.put_character)(target, c)
#define PUTS(s) (*targetClass.put_string)(target, s)
#define START(e) (*targetClass.start_element)(target, e, NULL, NULL, -1, NULL)
#define END(e) (*targetClass.end_element)(target, e, NULL)
#define FREE_TARGET (*targetClass._free)(target)
#define NEXT_CHAR HTGetCharacter()

/*	Module-wide variables
*/
static int finger_fd;		/* Socket for FingerHost */

struct _HTStructured {
    const HTStructuredClass *isa;	/* For gopher streams */
    /* ... */
};

static HTStructured *target;	/* The output sink */
static HTStructuredClass targetClass;	/* Copy of fn addresses */

/*	Initialisation for this module
 *	------------------------------
 */
static BOOL initialized = NO;
static BOOL initialize(void)
{
    finger_fd = -1;		/* Disconnected */
    return YES;
}

/*	Start anchor element
 *	--------------------
 */
static void start_anchor(const char *href)
{
    BOOL present[HTML_A_ATTRIBUTES];
    const char *value[HTML_A_ATTRIBUTES];

    {
	int i;

	for (i = 0; i < HTML_A_ATTRIBUTES; i++)
	    present[i] = (BOOL) (i == HTML_A_HREF);
    }
    ((const char **) value)[HTML_A_HREF] = href;
    (*targetClass.start_element) (target, HTML_A, present,
				  (const char **) value, -1, NULL);

}

/*	Send Finger Command line to remote host & Check Response
 *	--------------------------------------------------------
 *
 * On entry,
 *	command	points to the command to be sent, including CRLF, or is null
 *		pointer if no command to be sent.
 * On exit,
 *	Negative status indicates transmission error, socket closed.
 *	Positive status is a Finger status.
 */

static int response(char *command,
		    char *sitename,
		    HTParentAnchor *anAnchor,
		    HTFormat format_out,
		    HTStream *sink)
{
    int status;
    int length = (int) strlen(command);
    int ch, i;
    char line[BIG], *l, *cmd = NULL;
    char *p = line, *href = NULL;

    if (length == 0)
	return (-1);

    /* Set up buffering.
     */
    HTInitInput(finger_fd);

    /* Send the command.
     */
    CTRACE((tfp, "HTFinger command to be sent: %s", command));
    status = (int) NETWRITE(finger_fd, (char *) command, (unsigned) length);
    if (status < 0) {
	CTRACE((tfp, "HTFinger: Unable to send command. Disconnecting.\n"));
	NETCLOSE(finger_fd);
	finger_fd = -1;
	return status;
    }
    /* if bad status */
    /* Make a hypertext object with an anchor list.
     */
    target = HTML_new(anAnchor, format_out, sink);
    targetClass = *target->isa;	/* Copy routine entry points */

    /* Create the results report.
     */
    CTRACE((tfp, "HTFinger: Reading finger information\n"));
    START(HTML_HTML);
    PUTC('\n');
    START(HTML_HEAD);
    PUTC('\n');
    START(HTML_TITLE);
    PUTS("Finger server on ");
    PUTS(sitename);
    END(HTML_TITLE);
    PUTC('\n');
    END(HTML_HEAD);
    PUTC('\n');
    START(HTML_BODY);
    PUTC('\n');
    START(HTML_H1);
    PUTS("Finger server on ");
    START(HTML_EM);
    PUTS(sitename);
    END(HTML_EM);
    PUTS(": ");
    StrAllocCopy(cmd, command);
    for (i = ((int) strlen(cmd) - 1); i >= 0; i--) {
	if (cmd[i] == LF || cmd[i] == CR) {
	    cmd[i] = '\0';
	} else {
	    break;
	}
    }
    PUTS(cmd);
    FREE(cmd);
    END(HTML_H1);
    PUTC('\n');
    START(HTML_PRE);

    while ((ch = NEXT_CHAR) != EOF) {

	if (interrupted_in_htgetcharacter) {
	    CTRACE((tfp,
		    "HTFinger: Interrupted in HTGetCharacter, apparently.\n"));
	    _HTProgress(CONNECTION_INTERRUPTED);
	    goto end_html;
	}

	if (ch != LF) {
	    *p = (char) ch;	/* Put character in line */
	    if (p < &line[BIG - 1]) {
		p++;
	    }
	} else {
	    *p = '\0';		/* Terminate line */
	    /*
	     * OK we now have a line.
	     * Load it as 'l' and parse it.
	     */
	    p = l = line;
	    while (*l) {
		if (StrNCmp(l, STR_NEWS_URL, LEN_NEWS_URL) &&
		    StrNCmp(l, "snews://", 8) &&
		    StrNCmp(l, "nntp://", 7) &&
		    StrNCmp(l, "snewspost:", 10) &&
		    StrNCmp(l, "snewsreply:", 11) &&
		    StrNCmp(l, "newspost:", 9) &&
		    StrNCmp(l, "newsreply:", 10) &&
		    StrNCmp(l, "ftp://", 6) &&
		    StrNCmp(l, "file:/", 6) &&
		    StrNCmp(l, "finger://", 9) &&
		    StrNCmp(l, "http://", 7) &&
		    StrNCmp(l, "https://", 8) &&
		    StrNCmp(l, "wais://", 7) &&
		    StrNCmp(l, STR_MAILTO_URL, LEN_MAILTO_URL) &&
		    StrNCmp(l, "cso://", 6) &&
		    StrNCmp(l, "gopher://", 9))
		    PUTC(*l++);
		else {
		    StrAllocCopy(href, l);
		    start_anchor(strtok(href, " \r\n\t,>)\""));
		    while (*l && !StrChr(" \r\n\t,>)\"", *l))
			PUTC(*l++);
		    END(HTML_A);
		    FREE(href);
		}
	    }
	    PUTC('\n');
	}
    }
    NETCLOSE(finger_fd);
    finger_fd = -1;

  end_html:
    END(HTML_PRE);
    PUTC('\n');
    END(HTML_BODY);
    PUTC('\n');
    END(HTML_HTML);
    PUTC('\n');
    FREE_TARGET;
    return (0);
}

/*		Load by name					HTLoadFinger
 *		============
 */
int HTLoadFinger(const char *arg,
		 HTParentAnchor *anAnchor,
		 HTFormat format_out,
		 HTStream *stream)
{
    static char empty[1];

    char *username, *sitename;	/* Fields extracted from URL */
    char *slash, *at_sign;	/* Fields extracted from URL */
    char *command, *str, *param;	/* Buffers */
    int port;			/* Port number from URL */
    int status;			/* tcp return */
    int result = HT_LOADED;
    BOOL IsGopherURL = FALSE;
    const char *p1 = arg;

    CTRACE((tfp, "HTFinger: Looking for %s\n", (arg ? arg : "NULL")));

    if (!(arg && *arg)) {
	HTAlert(COULD_NOT_LOAD_DATA);
	return HT_NOT_LOADED;	/* Ignore if no name */
    }

    if (!initialized)
	initialized = initialize();
    if (!initialized) {
	HTAlert(gettext("Could not set up finger connection."));
	return HT_NOT_LOADED;	/* FAIL */
    }

    /*  Set up the host and command fields.
     */
    if (!strncasecomp(arg, "finger://", 9)) {
	p1 = arg + 9;		/* Skip "finger://" prefix */
    } else if (!strncasecomp(arg, "gopher://", 9)) {
	p1 = arg + 9;		/* Skip "gopher://" prefix */
	IsGopherURL = TRUE;
    }

    param = NULL;
    sitename = StrAllocCopy(param, p1);
    if (param == NULL) {
	HTAlert(COULD_NOT_LOAD_DATA);
	return HT_NOT_LOADED;
    } else if ((slash = StrChr(sitename, '/')) != NULL) {
	*slash++ = '\0';
	HTUnEscape(slash);
	if (IsGopherURL) {
	    if (*slash != '0') {
		HTAlert(COULD_NOT_LOAD_DATA);
		return HT_NOT_LOADED;	/* FAIL */
	    }
	    *slash++ = '\0';
	}
    }

    if ((at_sign = StrChr(sitename, '@')) != NULL) {
	if (IsGopherURL) {
	    HTAlert(COULD_NOT_LOAD_DATA);
	    return HT_NOT_LOADED;	/* FAIL */
	} else {
	    *at_sign++ = '\0';
	    username = sitename;
	    sitename = at_sign;
	    HTUnEscape(username);
	}
    } else if (slash) {
	username = slash;
    } else {
	username = empty;
    }

    if (*sitename == '\0') {
	HTAlert(gettext("Could not load data (no sitename in finger URL)"));
	result = HT_NOT_LOADED;	/* Ignore if no name */
    } else if (HTParsePort(sitename, &port) != NULL) {
	if (port != 79) {
	    HTAlert(gettext("Invalid port number - will only use port 79!"));
	    result = HT_NOT_LOADED;	/* Ignore if wrong port */
	}
    }

    if (result == HT_LOADED) {
	/* Load the string for making a connection/
	 */
	str = NULL;
	HTSprintf0(&str, "lose://%s/", sitename);

	/* Load the command for the finger server.
	 */
	command = NULL;
	if (at_sign && slash) {
	    if (*slash == 'w' || *slash == 'W') {
		HTSprintf0(&command, "/w %s%c%c", username, CR, LF);
	    } else {
		HTSprintf0(&command, "%s%c%c", username, CR, LF);
	    }
	} else if (at_sign) {
	    HTSprintf0(&command, "%s%c%c", username, CR, LF);
	} else if (*username == '/') {
	    if ((slash = StrChr((username + 1), '/')) != NULL) {
		*slash = ' ';
	    }
	    HTSprintf0(&command, "%s%c%c", username, CR, LF);
	} else if ((*username == 'w' || *username == 'W') &&
		   *(username + 1) == '/') {
	    if (*username + 2 != '\0') {
		*(username + 1) = ' ';
	    } else {
		*(username + 1) = '\0';
	    }
	    HTSprintf0(&command, "/%s%c%c", username, CR, LF);
	} else if ((*username == 'w' || *username == 'W') &&
		   *(username + 1) == '\0') {
	    HTSprintf0(&command, "/%s%c%c", username, CR, LF);
	} else if ((slash = StrChr(username, '/')) != NULL) {
	    *slash++ = '\0';
	    if (*slash == 'w' || *slash == 'W') {
		HTSprintf0(&command, "/w %s%c%c", username, CR, LF);
	    } else {
		HTSprintf0(&command, "%s%c%c", username, CR, LF);
	    }
	} else {
	    HTSprintf0(&command, "%s%c%c", username, CR, LF);
	}

	/* Now, let's get a stream setup up from the FingerHost:
	 * CONNECTING to finger host
	 */
	CTRACE((tfp, "HTFinger: doing HTDoConnect on '%s'\n", str));
	status = HTDoConnect(str, "finger", FINGER_PORT, &finger_fd);
	CTRACE((tfp, "HTFinger: Done DoConnect; status %d\n", status));

	if (status == HT_INTERRUPTED) {
	    /* Interrupt cleanly */
	    CTRACE((tfp,
		    "HTFinger: Interrupted on connect; recovering cleanly.\n"));
	    HTProgress(CONNECTION_INTERRUPTED);
	    result = HT_NOT_LOADED;
	} else if (status < 0) {
	    NETCLOSE(finger_fd);
	    finger_fd = -1;
	    CTRACE((tfp, "HTFinger: Unable to connect to finger host.\n"));
	    HTAlert(gettext("Could not access finger host."));
	    result = HT_NOT_LOADED;	/* FAIL */
	} else {
	    CTRACE((tfp, "HTFinger: Connected to finger host '%s'.\n", str));

	    /* Send the command, and process response if successful.
	     */
	    if (response(command, sitename, anAnchor, format_out, stream) != 0) {
		HTAlert(gettext("No response from finger server."));
		result = HT_NOT_LOADED;
	    }
	}
	FREE(str);
	FREE(command);
    }
    FREE(param);
    return result;
}

#ifdef GLOBALDEF_IS_MACRO
#define _HTFINGER_C_1_INIT { "finger", HTLoadFinger, NULL }
GLOBALDEF(HTProtocol, HTFinger, _HTFINGER_C_1_INIT);
#else
GLOBALDEF HTProtocol HTFinger =
{"finger", HTLoadFinger, NULL};
#endif /* GLOBALDEF_IS_MACRO */

#endif /* not DISABLE_FINGER */
