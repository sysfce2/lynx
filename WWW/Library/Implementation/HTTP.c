/*	HyperText Tranfer Protocol	- Client implementation		HTTP.c
**	==========================
** Modified:
** 27 Jan 1994  PDM  Added Ari Luotonen's Fix for Reload when using proxy
**                   servers.
*/

#include "HTUtils.h"
#include "tcp.h"

#include "HTTP.h"

#define HTTP_VERSION	"HTTP/1.0"

#define HTTP_PORT   80
#define HTTPS_PORT  443
#define SNEWS_PORT  563

#define HTTP_NETREAD   NETREAD
#define HTTP_NETWRITE  NETWRITE

#define INIT_LINE_SIZE		1024	/* Start with line buffer this big */
#define LINE_EXTEND_THRESH	256	/* Minimum read size */
#define VERSION_LENGTH 		20	/* for returned protocol version */

#include "HTParse.h"
#include "HTTCP.h"
#include "HTFormat.h"
#include "HTFile.h"
#include <ctype.h>
#include "HTAlert.h"
#include "HTMIME.h"
#include "HTML.h"
#include "HTInit.h"
#include "HTAABrow.h"

#include "LYLeaks.h"

/* #define TRACE 1 */

struct _HTStream 
{
  HTStreamClass * isa;
};

extern char * HTAppName;	/* Application name: please supply */
extern char * HTAppVersion;	/* Application version: please supply */
extern char * personal_mail_address;	/* User's name/email address */
extern char * LYUserAgent;	/* Lynx User-Agent string */
extern BOOL LYNoRefererHeader;	/* Never send Referer header? */
extern BOOL LYNoRefererForThis;	/* No Referer header for this URL? */
extern BOOL LYNoFromHeader;	/* Never send From header? */

extern BOOL using_proxy;	/* Are we using an HTTP gateway? */
PUBLIC BOOL reloading = FALSE;	/* Reloading => send no-cache pragma to proxy */
PUBLIC char * redirecting_url = NULL;	    /* Location: value. */
PUBLIC BOOL permanent_redirection = FALSE;  /* Got 301 status? */
PUBLIC BOOL redirect_post_content = FALSE;  /* Don't convert to GET? */

extern char LYUserSpecifiedURL; /* Is the URL a goto? */

extern BOOL keep_mime_headers;   /* Include mime headers and force source dump */
extern BOOL no_url_redirection;  /* Don't follow Location: URL for */
extern char *http_error_file;    /* Store HTTP status code in this file */
extern BOOL traversal;		 /* TRUE if we are doing a traversal */
extern BOOL dump_output_immediately;  /* TRUE if no interactive user */

extern char * HTLoadedDocumentURL NOPARAMS;
extern int HTCheckForInterrupt NOPARAMS;


/*		Load Document from HTTP Server			HTLoadHTTP()
**		==============================
**
**	Given a hypertext address, this routine loads a document.
**
**
** On entry,
**	arg	is the hypertext reference of the article to be loaded.
**
** On exit,
**	returns	>=0	If no error, a good socket number
**		<0	Error.
**
**	The socket must be closed by the caller after the document has been
**	read.
**
*/
PUBLIC int HTLoadHTTP ARGS4 (
	CONST char *, 		arg,
	HTParentAnchor *,	anAnchor,
	HTFormat,		format_out,
	HTStream*,		sink)
{
  int s;			/* Socket number for returned data */
  char *url = (char *)arg;	/* The URL which get_physical() returned */
  char *command=NULL;		/* The whole command */
  char *eol;			/* End of line if found */
  char *start_of_data;		/* Start of body of reply */
  int status;			/* tcp return */
  int bytes_already_read;
  char crlf[3];			/* A CR LF equivalent string */
  HTStream *target;		/* Unconverted data */
  HTFormat format_in;		/* Format arriving in the message */
  int do_head = 0;		/* Whether or not we should do a head */
  int do_post = 0;		/* ARE WE posting ? */
  char *METHOD;

  BOOL had_header;		/* Have we had at least one header? */
  char *line_buffer;
  char *line_kept_clean;
  BOOL extensions;		/* Assume good HTTP server */
  int compressed;
  char line[INIT_LINE_SIZE];
  char temp[80];
  BOOL first_Accept = TRUE;

  int length, doing_redirect, rv;
  int already_retrying = 0;
  int len = 0;

  if (anAnchor->isHEAD)
      do_head = TRUE;
  else if (anAnchor->post_data)
      do_post = TRUE;

  if (!url)
    {
      status = -3;
      _HTProgress ("Bad request.");
      goto done;
    }
  if (!*url) 
    {
      status = -2;
      _HTProgress ("Bad request.");
      goto done;
    }

  sprintf(crlf, "%c%c", CR, LF);

  /* At this point, we're talking HTTP/1.0. */
  extensions = YES;

 try_again:
  /* All initializations are moved down here from up above,
     so we can start over here... */
  eol = 0;
  bytes_already_read = 0;
  had_header = NO;
  length = 0;
  doing_redirect = 0;
  permanent_redirection = FALSE;
  redirect_post_content = FALSE;
  compressed = 0;
  target = NULL;
  line_buffer = NULL;
  line_kept_clean = NULL;

  if (!strncmp(url, "https", 5))
    {
      HTAlert("This client does not contain support for HTTPS URLs.");
      status = HT_NO_DATA;
      goto done;
    }
  status = HTDoConnect (arg, "HTTP", HTTP_PORT, &s);
  if (status == HT_INTERRUPTED)
    {
      /* Interrupt cleanly. */
      if (TRACE)
          fprintf (stderr,
                   "HTTP: Interrupted on connect; recovering cleanly.\n");
      _HTProgress ("Connection interrupted.");
      /* status already == HT_INTERRUPTED */
      goto done;
    }
  if (status < 0) 
    {
      if (TRACE) 
          fprintf(stderr, 
            "HTTP: Unable to connect to remote host for `%s' (errno = %d).\n",
	    url, SOCKET_ERRNO);
      HTAlert("Unable to connect to remote host.");
      status = HT_NO_DATA;
      goto done;
    }

  /*	Ask that node for the document,
  **	omitting the host name & anchor
  */
  {
    char * p1 = (HTParse(url, "", PARSE_PATH|PARSE_PUNCTUATION));

    if (do_post) {
        METHOD = "POST";
        StrAllocCopy(command, "POST ");
    } else if (do_head) {
        METHOD = "HEAD";
        StrAllocCopy(command, "HEAD ");
    } else {
        METHOD = "GET";
        StrAllocCopy(command, "GET ");
    }

    /* if we are using a proxy gateway don't copy in the first slash
     * of say: /gopher://a;lkdjfl;ajdf;lkj/;aldk/adflj
     * so that just gopher://.... is sent.
     */
    if (using_proxy)
        StrAllocCat(command, p1+1);
    else
        StrAllocCat(command, p1);
    FREE(p1);
  }
  if (extensions) 
    {
      StrAllocCat(command, " ");
      StrAllocCat(command, HTTP_VERSION);
    }

  StrAllocCat(command, crlf);	/* CR LF, as in rfc 977 */

  if (extensions) 
    {
      int n, i;
      char * host = NULL;
      extern char * language; /* Lynx's preferred language - FM */
      extern char * pref_charset; /* Lynx's preferred character set - MM */

      if ((host = HTParse(anAnchor->address, "", PARSE_HOST)) != NULL) {
	  sprintf(line, "Host: %s%c%c", host, CR,LF);
	  StrAllocCat(command, line);
	  FREE(host);
      }

      if (!HTPresentations)
          HTFormatInit();
      n = HTList_count(HTPresentations);

      first_Accept = TRUE;
      len = 0;
      for (i = 0; i < n; i++) {
          HTPresentation *pres =
			(HTPresentation *)HTList_objectAt(HTPresentations, i);
          if (pres->rep_out == WWW_PRESENT) {
	      if (pres->rep != WWW_SOURCE &&
		  strcasecomp(HTAtom_name(pres->rep), "www/mime") &&
		  strcasecomp(HTAtom_name(pres->rep), "www/compressed")) {
		  if (pres->quality < 1.0) {
		      if (pres->maxbytes > 0) {
		          sprintf(temp, ";q=%4.3f;mxb=%d",
			  		pres->quality, pres->maxbytes);
		      } else {
		          sprintf(temp, ";q=%4.3f", pres->quality);
		      }
		  } else if (pres->maxbytes > 0) {
		      sprintf(temp, ";mxb=%d", pres->maxbytes);
		  } else {
		      temp[0] = '\0';
		  }
                  sprintf(line, "%s%s%s",
		  		(first_Accept ?
				   "Accept: " : ", "),
				HTAtom_name(pres->rep),
				temp);
		  len += strlen(line);
		  if (len > 1000 && !first_Accept) {
		      StrAllocCat(command, crlf);
		      sprintf(line, "Accept: %s%s",
		      		    HTAtom_name(pres->rep),
				    temp);
		      len = strlen(line);
		  }
                  StrAllocCat(command, line);
		  first_Accept = FALSE;
	      }
          }
      }
      sprintf(line, "%s*/*;q=0.001%c%c",
      		    (first_Accept ?
		       "Accept: " : ", "), CR, LF);
      StrAllocCat(command, line);
      first_Accept = FALSE;
      len = 0;

      sprintf(line, "Accept-Encoding: %s, %s%c%c",
      		    "gzip", "compress", CR, LF);
      StrAllocCat(command, line);

      if (language && *language) {
          sprintf(line, "Accept-Language: %s%c%c", language, CR, LF);
	  StrAllocCat(command, line);
      }

      if (pref_charset && *pref_charset) {
	  StrAllocCat(command, "Accept-Charset: ");
	  strcpy(line, pref_charset);
	  if (line[strlen(line)-1] == ',')
	      line[strlen(line)-1] = '\0';
	  for (i = 0; line[i]; i++)
	      line[i] = TOLOWER(line[i]);
	  if (strstr(line, "iso-8859-1") == NULL)
	      strcat(line, ", iso-8859-1;q=0.001");
	  if (strstr(line, "us-ascii") == NULL)
	      strcat(line, ", us-ascii;q=0.001");
	  StrAllocCat(command, line);
	  sprintf(line, "%c%c", CR, LF);
	  StrAllocCat(command, line);
      }

      /*
       * When reloading give no-cache pragma to proxy server to make
       * it refresh its cache. -- Ari L. <luotonen@dxcern.cern.ch>
       *
       * Also send it as a Cache-Control header for HTTP/1.1. - FM
       */
      if (reloading) {
          sprintf(line, "Pragma: no-cache%c%c", CR, LF);
          StrAllocCat(command, line);
          sprintf(line, "Cache-Control: no-cache%c%c", CR, LF);
          StrAllocCat(command, line);
      }
      reloading = FALSE;           /* Now turn it off again if on */

      if (LYUserAgent && *LYUserAgent) {
          sprintf(line, "User-Agent: %s%c%c", LYUserAgent, CR, LF);
      } else {
          sprintf(line, "User-Agent: %s/%s  libwww-FM/%s%c%c",
                  HTAppName ? HTAppName : "unknown",
		  HTAppVersion ? HTAppVersion : "0.0",
		  HTLibraryVersion, CR, LF);
      }
      StrAllocCat(command, line);

      if (personal_mail_address && !LYNoFromHeader) {
          sprintf(line, "From: %s%c%c", personal_mail_address, CR,LF);
          StrAllocCat(command, line);
      }

      if (!(LYUserSpecifiedURL ||
      	    LYNoRefererHeader || LYNoRefererForThis) &&
         strcmp((char *)HTLoadedDocumentURL(), "")) {
          StrAllocCat(command, "Referer: ");
          StrAllocCat(command, (char *)HTLoadedDocumentURL());
          sprintf(line, "%c%c", CR, LF);
          StrAllocCat(command, line);
      }

      {
        char *docname;
        char *hostname;
        char *colon;
        int portnumber;
        char *auth;

        docname = HTParse(url, "", PARSE_PATH);
        hostname = HTParse(url, "", PARSE_HOST);
        if (hostname &&
            NULL != (colon = strchr(hostname, ':'))) 
          {
            *(colon++) = '\0';	/* Chop off port number */
            portnumber = atoi(colon);
          }
        else if (!strncmp(url, "https", 5))
	  {
	    portnumber = HTTPS_PORT;
	  }
        else 
	  {
	    portnumber = HTTP_PORT;
	  }

        if ((auth=HTAA_composeAuth(hostname, portnumber, docname)) != NULL &&
	     *auth != '\0') {
	    /*
	     *  If auth is not NULL nor zero-length, it's
	     *  an Authorization header to be included. - FM
	     */ 
            sprintf(line, "%s%c%c", auth, CR, LF);
            StrAllocCat(command, line);
	    if (TRACE)
                fprintf(stderr, "HTTP: Sending authorization: %s\n", auth);
	} else if (auth && *auth == '\0') {
	    /*
	     *  If auth is a zero-length string, the user either
	     *  cancelled or goofed at the username and password
	     *  prompt. - FM
	     */
	    HTAlert("Can't proceed without a username and password.");
	    FREE(command);
	    FREE(hostname);
	    FREE(docname);
	    status = HT_NO_DATA;
	    goto done;
        } else {
	    if (TRACE)
                fprintf(stderr, "HTTP: Not sending authorization (yet)\n");
        }
        FREE(hostname);
        FREE(docname);
      }
    }

  if (do_post)
    {
      if (TRACE)
          fprintf (stderr, "HTTP: Doing post, content-type '%s'\n",
                   anAnchor->post_content_type);
      sprintf (line, "Content-type: %s%c%c",
               anAnchor->post_content_type ? anAnchor->post_content_type 
							: "lose", CR, LF);
      StrAllocCat(command, line);
      {
        int content_length;
        if (!anAnchor->post_data)
          content_length = 4; /* 4 == "lose" :-) */
        else
          content_length = strlen (anAnchor->post_data);
        sprintf (line, "Content-length: %d%c%c",
                 content_length, CR, LF);
        StrAllocCat(command, line);
      }

      StrAllocCat(command, crlf);	/* Blank line means "end" */

      StrAllocCat(command, anAnchor->post_data);
    }

  StrAllocCat(command, crlf);	/* Blank line means "end" */

  if (TRACE)
      fprintf (stderr, "Writing:\n%s----------------------------------\n",
               command);

  _HTProgress ("Sending HTTP request.");

  status = HTTP_NETWRITE(s, command, (int)strlen(command));
  FREE(command);
  if (status <= 0) 
    {
      if (status == 0)
        {
          if (TRACE)
              fprintf (stderr, "HTTP: Got status 0 in initial write\n");
          /* Do nothing. */
        }
      else if 
        ((SOCKET_ERRNO == ENOTCONN || SOCKET_ERRNO == ECONNRESET ||
	  SOCKET_ERRNO == EPIPE) &&
         !already_retrying &&
         /* Don't retry if we're posting. */ !do_post)
          {
            /* Arrrrgh, HTTP 0/1 compability problem, maybe. */
            if (TRACE)
                fprintf (stderr, 
                 "HTTP: BONZO ON WRITE Trying again with HTTP0 request.\n");
            _HTProgress ("Retrying as HTTP0 request.");
            (void)NETCLOSE(s);
            extensions = NO;
            already_retrying = 1;
            goto try_again;
          }
      else
        {
          if (TRACE)
              fprintf (stderr,
	   "HTTP: Hit unexpected network WRITE error; aborting connection.\n");
          (void)NETCLOSE(s);
          status = -1;
          HTAlert("Unexpected network write error; connection aborted.");
          goto done;
        }
    }

  if (TRACE)
      fprintf (stderr, "HTTP: WRITE delivered OK\n");
  _HTProgress ("HTTP request sent; waiting for response.");

  /*	Read the first line of the response
   **	-----------------------------------
   */
  
  {
    /* Get numeric status etc */
    BOOL end_of_file = NO;
    int buffer_length = INIT_LINE_SIZE;

    line_buffer = (char *) malloc(buffer_length * sizeof(char));

    do 
      {	/* Loop to read in the first line */
        /* Extend line buffer if necessary for those crazy WAIS URLs ;-) */
        if (buffer_length - length < LINE_EXTEND_THRESH) 
          {
            buffer_length = buffer_length + buffer_length;
            line_buffer = 
              (char *) realloc(line_buffer, buffer_length * sizeof(char));
          }
        if (TRACE)
            fprintf (stderr, "HTTP: Trying to read %d\n",
                     buffer_length - length - 1);
        status = HTTP_NETREAD(s, line_buffer + length,
                              buffer_length - length - 1);
        if (TRACE)
            fprintf (stderr, "HTTP: Read %d\n", status);
        if (status <= 0)
          {
            /* Retry if we get nothing back too; 
               bomb out if we get nothing twice. */
            if (status == HT_INTERRUPTED)
              {
                if (TRACE)
                    fprintf (stderr, "HTTP: Interrupted initial read.\n");
                _HTProgress ("Connection interrupted.");
                status = HT_INTERRUPTED;
                goto clean_up;
              }
            else if 
              (status < 0 &&
               (SOCKET_ERRNO == ENOTCONN || SOCKET_ERRNO == ECONNRESET || 
		     SOCKET_ERRNO == EPIPE) && !already_retrying && !do_post)
              {
                /* Arrrrgh, HTTP 0/1 compability problem, maybe. */
                if (TRACE)
                    fprintf (stderr,
		    	"HTTP: BONZO Trying again with HTTP0 request.\n");
                (void)NETCLOSE(s);
                FREE(line_buffer);
                FREE(line_kept_clean);

                extensions = NO;
                already_retrying = 1;
                _HTProgress ("Retrying as HTTP0 request.");
                goto try_again;
              }
            else
              {
                if (TRACE)
                    fprintf (stderr,
  "HTTP: Hit unexpected network read error; aborting connection; status %d.\n",
			   status);
                HTAlert("Unexpected network read error; connection aborted.");
                (void)NETCLOSE(s);
                status = -1;
                goto clean_up;
              }
          }

        bytes_already_read += status;
        sprintf (line, "Read %d bytes of data.", bytes_already_read);
        HTProgress (line);

#ifdef UCX  /* UCX returns -1 on EOF */
        if (status == 0 || status == -1) 
#else
        if (status == 0)
#endif
          {
            end_of_file = YES;
            break;
          }
        line_buffer[length+status] = 0;

        if (line_buffer)
          {
            FREE(line_kept_clean);
            line_kept_clean = (char *)malloc (buffer_length * sizeof (char));
            memcpy(line_kept_clean, line_buffer, buffer_length);
          }

        eol = strchr(line_buffer + length, LF);
        /* Do we *really* want to do this? */
        if (eol && eol != line_buffer && *(eol-1) == CR) 
            *(eol-1) = ' '; 

        length = length + status;

        /* Do we really want to do *this*? */
        if (eol) 
            *eol = 0;		/* Terminate the line */
      }
    /* All we need is the first line of the response.  If it's a HTTP/1.0
       response, then the first line will be absurdly short and therefore
       we can safely gate the number of bytes read through this code
       (as opposed to below) to ~1000. */
    /* Well, let's try 100. */
    while (!eol && !end_of_file && bytes_already_read < 100);
  } /* Scope of loop variables */


  /*	We now have a terminated unfolded line. Parse it.
   **	-------------------------------------------------
   */
  if (TRACE)
      fprintf(stderr, "HTTP: Rx: %s\n", line_buffer);

/* Kludge to work with old buggy servers and the VMS Help gateway.
** They can't handle the third word, so we try again without it.
*/
  if (extensions &&       /* Old buggy server or Help gateway? */
      (0==strncmp(line_buffer,"<TITLE>Bad File Request</TITLE>",31) ||
       0==strncmp(line_buffer,"Address should begin with",25) ||
       0==strncmp(line_buffer,"<TITLE>Help ",12) ||
       0==strcmp(line_buffer,
       		 "Document address invalid or access not authorised"))) {
      FREE(line_buffer);
      FREE(line_kept_clean);
      extensions = NO;
      already_retrying = 1;
      if (TRACE)
          fprintf(stderr, "HTTP: close socket %d to retry with HTTP0\n", s);
      (void)NETCLOSE(s);
      /* print a progress message */
      _HTProgress ("Retrying as HTTP0 request.");
      goto try_again;
  }


  {
    int fields;
    char server_version[VERSION_LENGTH+1];
    int server_status;

    server_version[0] = 0;

    fields = sscanf(line_buffer, "%20s %d",
                    server_version,
                    &server_status);

    if (TRACE)
        fprintf (stderr, "HTTP: Scanned %d fields from line_buffer\n", fields);

    if (http_error_file) {     /* Make the status code externally available */
	FILE *error_file;
#ifdef SERVER_STATUS_ONLY
	error_file = fopen(http_error_file, "w");
	if (error_file) {		/* Managed to open the file */
	    fprintf(error_file, "error=%d\n", server_status);
#else
	error_file = fopen(http_error_file, "a");
	if (error_file) {		/* Managed to open the file */
	    fprintf(error_file, "   URL=%s (%s)\n", url, METHOD);
	    fprintf(error_file, "STATUS=%s\n", line_buffer);
#endif /* SERVER_STATUS_ONLY */
	    fclose(error_file);
	}
    }

    /*
     *  Rule out a non-HTTP/1.n reply as best we can.
     */
    if (fields < 2 || !server_version[0] || server_version[0] != 'H' ||
        server_version[1] != 'T' || server_version[2] != 'T' ||
        server_version[3] != 'P' || server_version[4] != '/' ||
        server_version[6] != '.') {
	/*
	 *  Ugh! An HTTP0 reply,
	 */
        HTAtom * encoding;

        if (TRACE)
            fprintf (stderr, "--- Talking HTTP0.\n");

        format_in = HTFileFormat(url, &encoding);
	/*
	 *  Treat all plain text as HTML.
         *  This sucks but its the only solution without
         *  without looking at content.
         */
        if (!strncmp(HTAtom_name(format_in), "text/plain",10)) {
            if (TRACE)
                fprintf(stderr,
                           "HTTP: format_in being changed to text/HTML\n");
            format_in = WWW_HTML;
        }

        start_of_data = line_kept_clean;
    } else {
        /*
	 *  Set up to decode full HTTP/1.n response. - FM
	 */
        format_in = HTAtom_for("www/mime");
        if (TRACE)
            fprintf (stderr, "--- Talking HTTP1.\n");

        /*
	 *  We set start_of_data to "" when !eol here because there
         *  will be a put_block done below; we do *not* use the value
         *  of start_of_data (as a pointer) in the computation of
         *  length (or anything else) when !eol.  Otherwise, set the
	 *  value of length to what we have beyond eol (i.e., beyond
	 *  the status line). - FM
	 */
        start_of_data = eol ? eol + 1 : "";
        length = eol ? length - (start_of_data - line_buffer) : 0;

	/*
	 *  Trim trailing spaces in line_buffer so that we can use
	 *  it in messages which include the status line. - FM
	 */
	while (line_buffer[strlen(line_buffer)-1] == ' ')
	       line_buffer[strlen(line_buffer)-1] = '\0';

	/*
	 *  Take appropriate actions based on the status. - FM
	 */
        switch (server_status/100) {
	  case 1:
	    /*
	     *  HTTP/1.1 Informational statuses.
	     *  100 Continue.
	     *  101 Switching Protocols.
	     *  > 101 is unknown.
	     *  We should never get these, and they have only
	     *  the status line and possibly other headers,
	     *  so we'll deal with them by showing the full
	     *  header to the user as text/plain. - FM
	     */
	    HTAlert("Got unexpected Informational Status.");
	    do_head = TRUE;
	    break;

          case 2:
	    /*
	     *  Good: Got MIME object! (Successful) - FM
	     */
	    switch (server_status) {
	      case 204:
	        /*
		 *  No Content.
		 */
	        HTAlert(line_buffer);
	        status = HT_NO_DATA;
	        goto done;
		break;

	      case 205:
	        /*
		 *  Reset Content.  The server has fulfilled the
		 *  request but nothing is returned and we should
		 *  reset any form content.  We'll instruct the
		 *  user to do that, and restore the current
		 *  document. - FM
		 */
	        HTAlert("Request fulfilled.  Reset Content.");
	        status = HT_NO_DATA;
	        goto done;
		break;

	      case 206:
	        /*
		 *  Partial Content.  We didn't send a Range
		 *  so something went wrong somewhere.  Show
		 *  the status message and restore the current
		 *  document. - FM
		 */
	        HTAlert(line_buffer);
                (void)NETCLOSE(s);
	        status = HT_NO_DATA;
	        goto done;
		break;

	      default:
	        /*
		 *  200 OK.
		 *  201 Created.
		 *  202 Accepted.
		 *  203 Non-Authoritative Information.
		 *  > 206 is unknown.
		 *  All should return something to display.
		 */
		HTProgress(line_buffer);
	    } /* case 2 switch */
            break;

          case 3:
	    /*
	     *  Various forms of Redirection. - FM
	     *  300 Multiple Choices.
	     *  301 Moved Permanently.
	     *  302 Moved Temporarily.
	     *  303 See Other.
	     *  304 Not Modified.
	     *  305 Use Proxy.
	     *  > 305 is unknown.
	     */
	    if (no_url_redirection || do_head || keep_mime_headers) {
	        /*
		 *  If any of these flags are set, we do not redirect,
		 *  but instead show what was returned to the user as
		 *  text/plain. - FM
		 */
		HTProgress(line_buffer);
	        break;
	    }

	    if (server_status == 300) { /* Multiple Choices */
	        /*
		 *  For client driven content negotiation.  The server
		 *  should be sending some way for the user-agent to
		 *  make a selection, so we'll show the user whatever
		 *  the server returns.  There might be a Location:
		 *  header with the server's preference present, but
		 *  the choice should be up to the user, someday based
		 *  on an Alternates: header, and a body always should
		 *  be present with descriptions and links for the
		 *  choices (i.e., we use the latter, for now). - FM
		 */
		HTAlert(line_buffer);
		break;
	    }

	    if (server_status == 304) { /* Not Modified */
	        /*
		 *  We didn't send an "If-Modified-Since" header,
		 *  so this status is inappropriate.  We'll deal
		 *  with it by showing the full header to the user
		 *  as text/plain. - FM
		 */
		HTAlert("Got unexpected 304 Not Modified status.");
	        do_head = TRUE;
		break;
	    }

	    if (server_status == 305) { /* Use Proxy */
		/*
		 *  We don't want to compound proxying, so if we
		 *  got this from a proxy, just show any message
		 *  to the user. - FM
		 */
		if (using_proxy) {
		    HTAlert("Got redirection to a proxy from the proxy!");
		    break;
		}
	    } else if (server_status > 305) {
	        /*
		 *  Show user the content, if any, for redirection
		 *  statuses we don't know. - FM
		 */
		HTAlert(line_buffer);
		break;
	    }

            /*
	     *  We do not load the file, but read the headers for
	     *  the "Location:", check out that redirecting_url
	     *  and if it's acceptible (e.g., not a telnet URL
	     *  when we have that disabled), initiate a new fetch.
	     *  If that's another redirecting_url, we'll repeat the
	     *  checks, and fetch initiations if acceptible, until
	     *  we reach the actual URL, or the redirection limit
	     *  set in HTAccess.c is exceeded.  If the status was 301,
	     *  indicating that the relocation is permanent, we set
	     *  the permanent_redirection flag to make it permanent
	     *  for the current anchor tree (i.e., will persist until
	     *  the tree is freed or the client exits).  If the
	     *  redirection would include POST content, we seek
	     *  confirmation from an interactive user, and otherwise
	     *  refuse the redirection.  We also don't allow permanent
	     *  redirection if we keep POST content.  If we don't find
	     *  the Location header or it's value is zero-length, we
	     *  display whatever the server returned, and the user
	     *  should RELOAD that to try again, or make a selection
	     *  from it if it contains links, or Left-Arrow to the
	     *  previous document. - FM
	     */
	    {
	      char *cp;

	      if ((dump_output_immediately || traversal) &&
	      	  do_post && server_status != 303) {
		  /*
		   *  Don't redirect POST content without approval
		   *  from an interactive user. - FM
		   */
		  (void)NETCLOSE(s);
		  status = -1;
		  HTAlert(
		       "Redirection of POST content requires user approval.");
		  if (traversal)
		      HTProgress(line_buffer);
		  goto clean_up;
	      }

	      /*
	       *  Get the rest of the headers and data, if
	       *  any, and then close the connection. - FM
	       */
	      while ((status = HTTP_NETREAD(s, line_buffer,
	      				    INIT_LINE_SIZE)) > 0) {
	          line_buffer[status] = '\0';
		  StrAllocCat(line_kept_clean, line_buffer);
	      }
	      (void)NETCLOSE(s);
              if (status == HT_INTERRUPTED) {
		  /*
		   *  Impatient user. - FM
		   */
                  if (TRACE)
                      fprintf (stderr, "HTTP: Interrupted followup read.\n");
                  _HTProgress ("Connection interrupted.");
                  status = HT_INTERRUPTED;
                  goto clean_up;
              }
	      doing_redirect = 1;
	      if (server_status == 301) { /* Moved Permanently */
	          HTProgress(line_buffer);
		  if (do_post) {
	              /*
		       *  Don't make the redirection permanent
		       *  if we have POST content. - FM
		       */
		      if (TRACE)
		          fprintf(stderr,
   "HTTP: Have POST content. Treating 301 (Permanent) as 302 (Temporary).\n");
		      HTAlert(
	 "Have POST content. Treating Permanent Redirection as Temporary.\n");
		  } else {
	              permanent_redirection = TRUE;
		  }
	      }

	      /*
	       *  Look for the "Location:" in the headers. - FM
	       */
	      cp = line_kept_clean;
	      while (*cp) {
	        if (*cp != 'l' && *cp != 'L') {
		    cp++;
	        } else if (!strncasecomp(cp, "Location:", 9)) {
	            char *cp1 = NULL, *cp2 = NULL;
	            cp += 9;
		    /*
		     *  Trim leading spaces. - FM
		     */
		    while (*cp == ' ')
		        cp++;
		    /*
		     *  Accept CRLF, LF, or CR as end of header. - FM
		     */
		    if (((cp1 = strchr(cp, LF)) != NULL) ||
		        (cp2 = strchr(cp, CR)) != NULL) {
			if (*cp1) {
			    *cp1 = '\0';
			    if ((cp2 = strchr(cp, CR)) != NULL)
			        *cp2 = '\0';
			} else {
			    *cp2 = '\0';
			}
			if (*cp == '\0') {
			    /*
			     *  The "Location:" value is zero-length, and
			     *  thus is probably something in the body, so
			     *  we'll show the user what was returned. - FM
			     */
			    if (TRACE)
			        fprintf(stderr,
					"HTTP: 'Location:' is zero-length!\n");
			    if (cp1)
			        *cp1 = LF;
			    if (cp2)
			        *cp2 = CR;
			    doing_redirect = 0;
			    permanent_redirection = FALSE;
			    start_of_data = line_kept_clean;
			    length = strlen(start_of_data);
			    HTAlert(
			       "Got redirection with a bad Location header.");
			    HTProgress(line_buffer);
			    break;
			}

			/*
			 *  Load the new URL into redirecting_url,
			 *  which will be checked in LYGetFile.c
			 *  for restrictions before seeking the
			 *  document at that Location. - FM
			 */
		        StrAllocCopy(redirecting_url, cp);
			HTProgress(line_buffer);
                        if (TRACE)
                            fprintf(stderr,
			    	    "HTTP: Picked up location '%s'\n", cp);
			if (cp1)
		            *cp1 = LF;
		        if (cp2)
		            *cp2 = CR;
			if (server_status == 305) { /* Use Proxy */
			    /*
			     *  Make sure the proxy field ends with
			     *  a slash. - FM
			     */
			    if (redirecting_url[strlen(redirecting_url)-1]
			    	!= '/')
				StrAllocCat(redirecting_url, "/");
			    /*
			     *  Append our URL. - FM
			     */
			    StrAllocCat(redirecting_url, anAnchor->address);
			    if (TRACE)
			        fprintf(stderr,
					"HTTP: Proxy URL is '%s'\n",
					redirecting_url);
			}
			if (!do_post || server_status == 303) {
			    /*
			     *  We don't have POST content (nor support PUT
			     *  or DELETE), or the status is "See Other" and
			     *  we can convert to GET, so go back and check
			     *  out the new URL. - FM
			     */
		            status = HT_REDIRECTING;
		            goto clean_up;
			}
			/*
			 *  Make sure the user wants to redirect
			 *  the POST content. - FM
			 */
			if (!HTConfirm(
				"Redirection for POST content. Proceed?")) {
			    doing_redirect = 0;
			    FREE(redirecting_url);
			    status = HT_NO_DATA;
			    goto clean_up;
			}
			/*
			 *  Set the flag to retain the POST content
			 *  and go back to check out the URL. - FM
			 */
			redirect_post_content = TRUE;
		        status = HT_REDIRECTING;
		        goto clean_up;
		    }
	            break;
	        } else {
		    /*
		     *  Keep looking for the Location header. - FM
		     */
	            cp++;
	        }
	      }

	      /*
	       *  If we get to here, we didn't find the Location
	       *  header, so we'll show the user what we got, if
	       *  anything. - FM
	       */
              if (TRACE)
                  fprintf (stderr, "HTTP: Failed to pick up location.\n");
	      doing_redirect = 0;
	      permanent_redirection = FALSE;
	      start_of_data = line_kept_clean;
	      length = strlen(start_of_data);
	      HTAlert("Got redirection with no Location header.");
	      HTProgress(line_buffer);
	      break;
	   }

          case 4:
	    /*
	     *  "I think I goofed!" (Client Error) - FM
	     */
            switch (server_status) {
              case 401:  /* Unauthorized */
	        /*
		 *  Authorization for orgin server required.
		 *  If we can set up authorization based on
		 *  the WWW-Authenticate header, and the user
		 *  provides a username and password, try again.
		 *  Otherwise, issue a statusline message and
		 *  restore the current document.  - FM
		 */
		if (HTAA_shouldRetryWithAuth(start_of_data, length, NULL, s)) 
                  {
 		    extern char *authentication_info[2];

                    (void)NETCLOSE(s);
                    if (dump_output_immediately && !authentication_info[0]) {
                        fprintf(stderr,
		      		"HTTP: Access authorization required.\n");
                        fprintf(stderr,
		      		"       Use the -auth=id:pw parameter.\n");
                        status = HT_NO_DATA;
                        goto clean_up;
                    }

                    if (TRACE) 
                        fprintf(stderr, "%s %d %s\n",
                              "HTTP: close socket", s,
                              "to retry with Access Authorization");

                    _HTProgress (
		    	"Retrying with access authorization information.");
		    FREE(line_buffer);
		    FREE(line_kept_clean);
                    goto try_again;
                    break;
                  }
		else
		  {
		    HTAlert(
	"Can't retry with authorization!  Contact the server's WebMaster.");
		    (void)NETCLOSE(s);
                    status = -1;
                    goto clean_up;
		  }

	      case 407:
	        /*
		 *  Proxy Authentication Required.  We'll handle
		 *  Proxy-Authenticate headers and retry with a
		 *  Proxy-Authorization header, someday, but for
		 *  now, apologized to the user and restore the
		 *  current document. - FM
		 */
	        HTAlert(
		 "Proxy Authentication Required.  Sorry, not yet supported.");
                (void)NETCLOSE(s);
	        status = HT_NO_DATA;
	        goto done;
		break;

	      case 408:
	        /*
		 *  Request Timeout.  Show the status message
		 *  and restore the current document. - FM
		 */
	        HTAlert(line_buffer);
                (void)NETCLOSE(s);
	        status = HT_NO_DATA;
	        goto done;
		break;

              default:
                /*
		 *  400 Bad Request.
		 *  402 Payment Required.
		 *  403 Forbidden.
		 *  404 Not Found.
		 *  405 Method Not Allowed.
		 *  406 Not Acceptable.
		 *  409 Conflict.
		 *  410 Gone.
		 *  411 Length Required.
		 *  412 Precondition Failed.
		 *  413 Request Entity Too Large.
		 *  414 Request-URI Too Long.
		 *  415 Unsupported Media Type.
		 *  416 List Response (for content negotiation).
		 *  > 416 is unknown.
		 *  Show the status message, and display
		 *  the returned text if we are not doing
		 *  a traversal. - FM
		 */
		HTAlert(line_buffer);
		if (traversal) {
		    (void)NETCLOSE(s);
		    status = -1;
		    goto clean_up;
		}
                break;
            } /* case 4 switch */
            break;

          case 5:
	    /*
	     *  "I think YOU goofed!" (server error)
	     *  500 Internal Server Error
	     *  501 Not Implemented
	     *  502 Bad Gateway
	     *  503 Service Unavailable
	     *  504 Gateway Timeout
	     *  505 HTTP Version Not Supported
	     *  > 505 is unknown.
	     *  Should always include a message, which
	     *  we always should display. - FM
	     */
	    HTAlert(line_buffer);
            break;

          default:
	    /*
	     *  Bad or unknown server_status number.
	     *  Take a chance and hope there is
	     *  something to display. - FM
	     */
            HTAlert("Unknown status reply from server!");
	    HTAlert(line_buffer);
	    if (traversal) {
		(void)NETCLOSE(s);
		status = -1;
		goto clean_up;
	    }
            break;
        } /* Switch on server_status/100 */

      }	/* Full HTTP reply */
  } /* scope of fields */

  /* Set up the stream stack to handle the body of the message */
  if (do_head || keep_mime_headers) {
      /* It was a HEAD request, or we want the headers and source */
      start_of_data = line_kept_clean;
      length = strlen(start_of_data);
      format_in = HTAtom_for("text/plain");
  }

  target = HTStreamStack(format_in,
                         format_out,
                         sink, anAnchor);

  if (!target || target == NULL) 
    {
      char buffer[1024];	/* @@@@@@@@ */

      (void)NETCLOSE(s);
      sprintf(buffer, "Sorry, no known way of converting %s to %s.",
              HTAtom_name(format_in), HTAtom_name(format_out));
      _HTProgress (buffer);
      status = -1;
      goto clean_up;
    }

  /* Recycle the first chunk of data, in all cases. */
  (*target->isa->put_block)(target, start_of_data, length);

  /* Go pull the bulk of the data down. */
  rv = HTCopy(s, NULL, target);

  if (rv == -1)
    {
      /* Intentional interrupt before data were received, not an error */
      /* (*target->isa->_abort)(target, NULL); */ /* already done in HTCopy */
      status = HT_INTERRUPTED;
      (void)NETCLOSE(s);
      goto clean_up;
    }
  if (rv == -2 && !already_retrying && !do_post)
    { 
      /* Aw hell, a REAL error, maybe cuz it's a dumb HTTP0 server */
      if (TRACE)
          fprintf (stderr, "HTTP: Trying again with HTTP0 request.\n");
      /* May as well consider it an interrupt -- right? */
      (*target->isa->_abort)(target, NULL);
      (void)NETCLOSE(s);
      FREE(line_buffer);
      FREE(line_kept_clean);
      extensions = NO;
      already_retrying = 1;
      _HTProgress ("Retrying as HTTP0 request.");
      goto try_again;
    }

  /* 
   * Close socket if partial transmission (was freed on abort)
   * Free if complete transmission (socket was closed before return)
   */
  if (rv == HT_INTERRUPTED)
    {
      (void)NETCLOSE(s);
    }
  else
      (*target->isa->_free)(target);

  if (doing_redirect)
    /*
     * We already jumped over all this if the "case 3:" code worked
     * above, but we'll check here as a backup in case it fails. - FM
     */
    {
      /* Lou's old comment:  - FM */
      /* OK, now we've got the redirection URL temporarily stored
         in external variable redirecting_url, exported from HTMIME.c,
         since there's no straightforward way to do this in the library
         currently.  Do the right thing. */
      status = HT_REDIRECTING;
    }
  else
    {
      /* If any data were received, treat as a complete transmission */
      status = HT_LOADED;
    }

  /*	Clean up
   */
clean_up:
  FREE(line_buffer);
  FREE(line_kept_clean);

done:
  /* Clear out on exit, just in case. */
  do_head = 0;
  do_post = 0;
  return status;
}


/*	Protocol descriptor
*/

#ifdef GLOBALDEF_IS_MACRO
#define _HTTP_C_GLOBALDEF_1_INIT { "http", HTLoadHTTP, 0}
GLOBALDEF (HTProtocol,HTTP,_HTTP_C_GLOBALDEF_1_INIT);
#define _HTTP_C_GLOBALDEF_2_INIT { "https", HTLoadHTTP, 0}
GLOBALDEF (HTProtocol,HTTPS,_HTTP_C_GLOBALDEF_2_INIT);
#else
GLOBALDEF PUBLIC HTProtocol HTTP = { "http", HTLoadHTTP, 0 };
GLOBALDEF PUBLIC HTProtocol HTTPS = { "https", HTLoadHTTP, 0 };
#endif /* GLOBALDEF_IS_MACRO */
