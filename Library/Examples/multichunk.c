#include "WWWLib.h"
#include "WWWHTTP.h"
#include "WWWApp.h"
#include "WWWInit.h"

#ifdef LIBWWW_SHARED
#include "HTextImp.h"
#endif

#define MAX_COUNT	1024

int main (int argc, char ** argv)
{
    HTRequest * request = NULL;
    HTChunk * chunk = NULL;
    char * uri = NULL;
    int arg = 0;
    int maxcount = 1;

    for (arg=1; arg<argc; arg++) {
	if (*argv[arg] == '-') {
	    if (!strcmp(argv[arg], "-n")) {
		maxcount  = (arg+1 < argc && *argv[arg+1] != '-') ?
		    atoi(argv[++arg]) : 1;
		
	    } else {
		fprintf(stderr, "Bad Argument (%s)\n", argv[arg]);
	    }
	} else {
	    uri = argv[arg];
	}
    }

    /* Initialize libwww core */
    HTProfile_newPreemptiveClient("TestApp", "1.0");

    /* Don't say anything */
    HTAlert_setInteractive(NO);

    /* Turn on TRACE so we can see what is going on */
#if 0
    WWWTRACE = SHOW_CORE_TRACE + SHOW_STREAM_TRACE + SHOW_PROTOCOL_TRACE;
#endif

    if (uri && maxcount<MAX_COUNT) {
	char * cwd = HTGetCurrentDirectoryURL();
	char * absolute_uri = HTParse(uri, cwd, PARSE_ALL);
	time_t local = time(NULL);
	ms_t start = HTGetTimeInMillis();
	ms_t end = -1;
	int cnt;
	fprintf(stdout, "Starting downloading %s %d time(s) at %s\n",
		uri, maxcount, HTDateTimeStr(&local, YES));
	for (cnt=0; cnt<maxcount; cnt++) {
	    request = HTRequest_new();

	    /* Set up the request and pass it to the Library */
	    HTRequest_setOutputFormat(request, WWW_SOURCE);
	    if (uri) {
		chunk = HTLoadToChunk(absolute_uri, request);

		/* If chunk != NULL then we have the data */
		if (chunk) {
		    char * string = HTChunk_toCString(chunk);
		    HT_FREE(string);
		}
	    }

	    /* Clean up the request */
	    HTRequest_delete(request);

	}

	local = time(NULL);
	end = HTGetTimeInMillis();
	fprintf(stdout, "Ending at %s - spent %ld ms\n",
		HTDateTimeStr(&local, YES), end-start);

	HT_FREE(absolute_uri);
	HT_FREE(cwd);
    } else {
	fprintf(stderr, "Downloads the same URI n times\n");
	fprintf(stderr, "Syntax: %s -n <count> <URI>\n", argv[0]);
    }

    /* Terminate the Library */
    HTProfile_delete();
    fprintf(stdout, "\n");
    return 0;
}
