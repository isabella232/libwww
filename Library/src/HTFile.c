/*			File Access				HTFile.c
**			===========
**
**	This is unix-specific code in general, with some VMS bits.
**	These are routines for file access used by WWW browsers.
**
** History:
**	   Feb 91	Written Tim Berners-Lee CERN/CN
**	   Apr 91	vms-vms access included using DECnet syntax
**	26 Jun 92 (JFG) When running over DECnet, suppressed FTP.
**			Fixed access bug for relative names on VMS.
**
** Bugs:
**	FTP: Cannot access VMS files from a unix machine.
**      How can we know that the
**	target machine runs VMS?
**
**	DIRECTORIES are NOT SORTED.. alphabetically etc.
*/

#include "HTFile.h"		/* Implemented here */


#define INFINITY 512		/* file name length @@ FIXME */
#define MULTI_SUFFIX ".multi"   /* Extension for scanning formats */

#include "HTUtils.h"

#include "WWW.h"
#include "HTParse.h"
#include "tcp.h"
#include "HTTCP.h"
#ifndef DECNET
#include "HTFTP.h"
#endif
#include "HTAnchor.h"
#include "HTAtom.h"
#include "HTWriter.h"
#include "HTFWriter.h"
#include "HTInit.h"

typedef struct _HTSuffix {
	char *		suffix;
	HTAtom *	rep;
	float		quality;
} HTSuffix;



#ifdef unix			/* if this is to compile on a UNIX machine */
#include "HTML.h"		/* For directory object building */

#define PUTC(c) (*targetClass.put_character)(target, c)
#define PUTS(s) (*targetClass.put_string)(target, s)
#define START(e) (*targetClass.start_element)(target, e, 0, 0)
#define END(e) (*targetClass.end_element)(target, e)
#define END_TARGET (*targetClass.end_document)(target)
#define FREE_TARGET (*targetClass.free)(target)
struct _HTStructured {
	CONST HTStructuredClass *	isa;
	/* ... */
};

#endif				/* unix */


/*                   Controlling globals
**
*/

PUBLIC int HTDirAccess = HT_DIR_OK;
PUBLIC int HTDirReadme = HT_DIR_README_TOP;

PRIVATE char *HTMountRoot = "/Net/";		/* Where to find mounts */
#ifdef vms
PRIVATE char *HTCacheRoot = "/WWW$SCRATCH/";   /* Where to cache things */
#else
PRIVATE char *HTCacheRoot = "/tmp/W3_Cache_";   /* Where to cache things */
#endif

/* PRIVATE char *HTSaveRoot  = "$(HOME)/WWW/";*/    /* Where to save things */


/*	Suffix registration
*/

PRIVATE HTList * HTSuffixes = 0;

/*	Define the representation associated with a file suffix
**	-------------------------------------------------------
*/
PUBLIC void HTSetSuffix ARGS3(
	CONST char *,	suffix,
	CONST char *,	representation,
	float,		value)
{
    
    HTSuffix * suff = (HTSuffix*) calloc(1, sizeof(HTSuffix));
    if (suff == NULL) outofmem(__FILE__, "HTSetSuffix");
    
    if (!HTSuffixes) HTSuffixes = HTList_new();
    
    StrAllocCopy(suff->suffix, suffix);
    suff->rep = HTAtom_for(representation);
    suff->quality = value;
    HTList_addObject(HTSuffixes, suff);
}




#ifdef vms
/*	Convert unix-style name into VMS name
**	-------------------------------------
**
** Bug:	Returns pointer to static -- non-reentrant
*/
PRIVATE char * vms_name(CONST char * nn, CONST char * fn)
{

/*	We try converting the filename into Files-11 syntax. That is, we assume
**	first that the file is, like us, on a VMS node. We try remote
**	(or local) DECnet access. Files-11, VMS, VAX and DECnet
**	are trademarks of Digital Equipment Corporation. 
**	The node is assumed to be local if the hostname WITHOUT DOMAIN
**	matches the local one. @@@
*/
    static char vmsname[INFINITY];	/* returned */
    char * filename = (char*)malloc(strlen(fn)+1);
    char * nodename = (char*)malloc(strlen(nn)+2+1);	/* Copies to hack */
    char *second;		/* 2nd slash */
    char *last;			/* last slash */
    
    char * hostname = HTHostName();

    if (!filename || !nodename) outofmem(__FILE__, "vms_name");
    strcpy(filename, fn);
    strcpy(nodename, "");	/* On same node? Yes if node names match */
    {
        char *p, *q;
        for (p=hostname, q=nn; *p && *p!='.' && *q && *q!='.'; p++, q++){
	    if (TOUPPER(*p)!=TOUPPER(*q)) {
	        strcpy(nodename, nn);
		q = strchr(nodename, '.');	/* Mismatch */
		if (q) *q=0;			/* Chop domain */
		strcat(nodename, "::");		/* Try decnet anyway */
		break;
	    }
	}
    }

    second = strchr(filename+1, '/');		/* 2nd slash */
    last = strrchr(filename, '/');	/* last slash */
        
    if (!second) {				/* Only one slash */
	sprintf(vmsname, "%s%s", nodename, filename + 1);
    } else if(second==last) {		/* Exactly two slashes */
	*second = 0;		/* Split filename from disk */
	sprintf(vmsname, "%s%s:%s", nodename, filename+1, second+1);
	*second = '/';	/* restore */
    } else { 				/* More than two slashes */
	char * p;
	*second = 0;		/* Split disk from directories */
	*last = 0;		/* Split dir from filename */
	sprintf(vmsname, "%s%s:[%s]%s",
		nodename, filename+1, second+1, last+1);
	*second = *last = '/';	/* restore filename */
	for (p=strchr(vmsname, '['); *p!=']'; p++)
	    if (*p=='/') *p='.';	/* Convert dir sep.  to dots */
    }
    free(nodename);
    free(filename);
    return vmsname;
}


#endif /* vms */



/*	Send README file
**
**  If a README file exists, then it is inserted into the document here.
*/

#ifdef unix
PRIVATE void do_readme ARGS2(HTStructured *, target, CONST char *, localname)
{ 
    FILE * fp;
    char * readme_file_name = 
	malloc(strlen(localname)+ 1 + strlen(HT_DIR_README_FILE) + 1);
    strcpy(readme_file_name, localname);
    strcat(readme_file_name, "/");
    strcat(readme_file_name, HT_DIR_README_FILE);
    
    fp = fopen(readme_file_name,  "r");
    
    if (fp) {
	HTStructuredClass targetClass;
	
	targetClass =  *target->isa;	/* (Can't init agregate in K&R) */
	START(HTML_PRE);
	for(;;){
	    char c = fgetc(fp);
	    if (c == (char)EOF) break;
	    switch (c) {
	    	case '&':
		case '<':
		case '>':
			PUTC('&');
			PUTC('#');
			PUTC((char)(c / 10));
			PUTC((char) (c % 10));
			PUTC(';');
			break;
	    	case '\n':
			PUTC('\r');	/* Fall through */
		default:
			PUTC(c);
	    }
	}
	END(HTML_PRE);
	fclose(fp);
    } 
}
#endif


/*	Make the cache file name for a W3 document
**	------------------------------------------
**	Make up a suitable name for saving the node in
**
**	E.g.	/tmp/WWW_Cache_news/1234@cernvax.cern.ch
**		/tmp/WWW_Cache_http/crnvmc/FIND/xx.xxx.xx
**
** On exit,
**	returns	a malloc'ed string which must be freed by the caller.
*/
PUBLIC char * HTCacheFileName ARGS1(CONST char *,name)
{
    char * access = HTParse(name, "", PARSE_ACCESS);
    char * host = HTParse(name, "", PARSE_HOST);
    char * path = HTParse(name, "", PARSE_PATH+PARSE_PUNCTUATION);
    
    char * result;
    result = (char *)malloc(
	    strlen(HTCacheRoot)+strlen(access)
	    +strlen(host)+strlen(path)+6+1);
    if (result == NULL) outofmem(__FILE__, "HTCacheFileName");
    sprintf(result, "%s/WWW/%s/%s%s", HTCacheRoot, access, host, path);
    free(path);
    free(access);
    free(host);
    return result;
}


/*	Open a file for write, creating the path
**	----------------------------------------
*/
#ifdef NOT_IMPLEMENTED
PRIVATE int HTCreatePath ARGS1(CONST char *,path)
{
    return -1;
}
#endif

/*	Convert filenames between local and WWW formats
**	-----------------------------------------------
**	Make up a suitable name for saving the node in
**
**	E.g.	$(HOME)/WWW/news/1234@cernvax.cern.ch
**		$(HOME)/WWW/http/crnvmc/FIND/xx.xxx.xx
**
** On exit,
**	returns	a malloc'ed string which must be freed by the caller.
*/
PUBLIC char * HTLocalName ARGS1(CONST char *,name)
{
    char * access = HTParse(name, "", PARSE_ACCESS);
    char * host = HTParse(name, "", PARSE_HOST);
    char * path = HTParse(name, "", PARSE_PATH+PARSE_PUNCTUATION);
    
    if (0==strcmp(access, "file")) {
        free(access);	
	if ((0==strcmp(host, HTHostName())) || !*host) {
	    free(host);
	    if (TRACE) fprintf(stderr, "Node `%s' means path `%s'\n", name, path);
	    return(path);
	} else {
	    char * result = (char *)malloc(
	    			strlen("/Net/")+strlen(host)+strlen(path)+1);
              if (result == NULL) outofmem(__FILE__, "HTLocalName");
	    sprintf(result, "%s%s%s", "/Net/", host, path);
	    free(host);
	    free(path);
	    if (TRACE) fprintf(stderr, "Node `%s' means file `%s'\n", name, result);
	    return result;
	}
    } else {  /* other access */
	char * result;
        CONST char * home =  (CONST char*)getenv("HOME");
	if (!home) home = "/tmp"; 
	result = (char *)malloc(
		strlen(home)+strlen(access)+strlen(host)+strlen(path)+6+1);
      if (result == NULL) outofmem(__FILE__, "HTLocalName");
	sprintf(result, "%s/WWW/%s/%s%s", home, access, host, path);
	free(path);
	free(access);
	free(host);
	return result;
    }
}


/*	Make a WWW name from a full local path name
**
** Bugs:
**	At present, only the names of two network root nodes are hand-coded
**	in and valid for the NeXT only. This should be configurable in
**	the general case.
*/

PUBLIC char * WWW_nameOfFile ARGS1 (CONST char *,name)
{
    char * result;
#ifdef NeXT
    if (0==strncmp("/private/Net/", name, 13)) {
	result = (char *)malloc(7+strlen(name+13)+1);
	if (result == NULL) outofmem(__FILE__, "WWW_nameOfFile");
	sprintf(result, "file://%s", name+13);
    } else
#endif
    if (0==strncmp(HTMountRoot, name, 5)) {
	result = (char *)malloc(7+strlen(name+5)+1);
	if (result == NULL) outofmem(__FILE__, "WWW_nameOfFile");
	sprintf(result, "file://%s", name+5);
    } else {
        result = (char *)malloc(7+strlen(HTHostName())+strlen(name)+1);
	if (result == NULL) outofmem(__FILE__, "WWW_nameOfFile");
	sprintf(result, "file://%s%s", HTHostName(), name);
    }
    if (TRACE) fprintf(stderr, "File `%s'\n\tmeans node `%s'\n", name, result);
    return result;
}


/*	Determine a suitable suffix, given the representation
**	-----------------------------------------------------
**
** On entry,
**	rep	is the atomized MIME style representation
**
** On exit,
**	returns	a pointer to a suitable suffix string if one has been
**		found, else "".
*/
PUBLIC CONST char * HTFileSuffix ARGS1(HTAtom*, rep)
{
    HTSuffix * suff;
    int n;
    int i;

#ifndef NO_INIT    
    if (!HTSuffixes) HTFileInit();
#endif
    n = HTList_count(HTSuffixes);
    for(i=0; i<n; i++) {
	suff = HTList_objectAt(HTSuffixes, i);
	if (suff->rep == rep) {
	    return suff->suffix;		/* OK -- found */
	}
    }
    return "";		/* Dunno */
}


/*	Determine file format from file name
**	------------------------------------
**
*/

PUBLIC HTFormat HTFileFormat ARGS1 (CONST char *,filename)

{
    HTSuffix * suff;
    int n;
    int i;
    int lf = strlen(filename);

#ifndef NO_INIT    
    if (!HTSuffixes) HTFileInit();
#endif
    n = HTList_count(HTSuffixes);
    for(i=0; i<n; i++) {
        int ls;
	suff = HTList_objectAt(HTSuffixes, i);
	ls = strlen(suff->suffix);
	if ((ls <= lf) && 0==strcmp(suff->suffix, filename + lf - ls)) {
	    return suff->rep;		/* OK -- found */
	}
    }
    return WWW_BINARY;		/* Dunno */
}


/*	Determine value from file name
**	------------------------------
**
*/

PUBLIC float HTFileValue ARGS1 (CONST char *,filename)

{
    HTSuffix * suff;
    int n;
    int i;
    int lf = strlen(filename);

#ifndef NO_INIT    
    if (!HTSuffixes) HTFileInit();
#endif
    n = HTList_count(HTSuffixes);
    for(i=0; i<n; i++) {
        int ls;
	suff = HTList_objectAt(HTSuffixes, i);
	ls = strlen(suff->suffix);
	if ((ls <= lf) && 0==strcmp(suff->suffix, filename + lf - ls)) {
	    if (TRACE) fprintf(stderr, "File: Value of %s is %.3f\n",
			       filename, suff->quality);
	    return suff->quality;		/* OK -- found */
	}
    }
    return 0.3;		/* Dunno! */
}


/*	Determine write access to a file
**	--------------------------------
**
** On exit,
**	return value	YES if file can be accessed and can be written to.
**
** Bugs:
**	1.	No code for non-unix systems.
**	2.	Isn't there a quicker way?
*/

#ifdef vms
#define NO_GROUPS
#endif
#ifdef NO_UNIX_IO
#define NO_GROUPS
#endif
#ifdef PCNFS
#define NO_GROUPS
#endif

PUBLIC BOOL HTEditable ARGS1 (CONST char *,filename)
{
#ifdef NO_GROUPS
    return NO;		/* Safe answer till we find the correct algorithm */
#else
    int 	groups[NGROUPS];	
    uid_t	myUid;
    int		ngroups;			/* The number of groups  */
    struct stat	fileStatus;
    int		i;
        
    if (stat(filename, &fileStatus))		/* Get details of filename */
    	return NO;				/* Can't even access file! */

    ngroups = getgroups(NGROUPS, groups);	/* Groups to which I belong  */
    myUid = geteuid();				/* Get my user identifier */

    if (TRACE) {
        int i;
	printf("File mode is 0%o, uid=%d, gid=%d. My uid=%d, %d groups (",
    	    fileStatus.st_mode, fileStatus.st_uid, fileStatus.st_gid,
	    myUid, ngroups);
	for (i=0; i<ngroups; i++) printf(" %d", groups[i]);
	printf(")\n");
    }
    
    if (fileStatus.st_mode & 0002)		/* I can write anyway? */
    	return YES;
	
    if ((fileStatus.st_mode & 0200)		/* I can write my own file? */
     && (fileStatus.st_uid == myUid))
    	return YES;

    if (fileStatus.st_mode & 0020)		/* Group I am in can write? */
    {
   	for (i=0; i<ngroups; i++) {
            if (groups[i] == fileStatus.st_gid)
	        return YES;
	}
    }
    if (TRACE) fprintf(stderr, "\tFile is not editable.\n");
    return NO;					/* If no excuse, can't do */
#endif
}


/*	Make a save stream
**	------------------
**
**	The stream must be used for writing back the file.
**	@@@ no backup done
*/
PUBLIC HTStream * HTFileSaveStream ARGS1(HTParentAnchor *, anchor)
{

    CONST char * addr = HTAnchor_address((HTAnchor*)anchor);
    char *  localname = HTLocalName(addr);
    
    FILE* fp = fopen(localname, "w");
    if (!fp) return NULL;
    
    return HTFWriter_new(fp);
    
}


/*	Load a document
**	---------------
**
** On entry,
**	addr		must point to the fully qualified hypertext reference.
**			This is the physsical address of the file
**
** On exit,
**	returns		<0		Error has occured.
**			HTLOADED	OK 
**
*/
PUBLIC int HTLoadFile ARGS4 (
	CONST char *,		addr,
	HTParentAnchor *,	anchor,
	HTFormat,		format_out,
	HTStream *,		sink
)
{
    char * filename;
    HTFormat format;
    int fd = -1;		/* Unix file descriptor number = INVALID */
    char * nodename = 0;
    char * newname=0;	/* Simplified name of file */

/*	Reduce the filename to a basic form (hopefully unique!)
*/
    StrAllocCopy(newname, addr);
    filename=HTParse(newname, "", PARSE_PATH|PARSE_PUNCTUATION);
    nodename=HTParse(newname, "", PARSE_HOST);
    free(newname);
    
    format = HTFileFormat(filename);

    
#ifdef vms
/* Assume that the file is in Unix-style syntax if it contains a '/'
   after the leading one @@ */
    {
	char * vmsname = strchr(filename + 1, '/') ?
	  vms_name(nodename, filename) : filename + 1;
	fd = open(vmsname, O_RDONLY, 0);
	
/*	If the file wasn't VMS syntax, then perhaps it is ultrix
*/
	if (fd<0) {
	    char ultrixname[INFINITY];
	    if (TRACE) fprintf(stderr, "HTFile: Can't open as %s\n", vmsname);
	    sprintf(ultrixname, "%s::\"%s\"", nodename, filename);
	    fd = open(ultrixname, O_RDONLY, 0);
	    if (fd<0) {
		if (TRACE) fprintf(stderr, 
				   "HTFile: Can't open as %s\n", ultrixname);
	    }
	}
    }
#else

/*	For unix, we try to translate the name into the name of a transparently
**	mounted file.
**
**	Not allowed in secure (HTClienntHost) situations TBL 921019
*/
#ifndef NO_UNIX_IO
    /* @@@@ Need protection here for telnet server but not httpd server */
	 
    /* if (!HTClientHost) */ {		/* try local unix file system */
	char * localname = HTLocalName(addr);
	struct stat dir_info;
	
#ifdef GOT_READ_DIR

/*			  Multiformat handling
**
**	If needed, scan directory to find a good file.
**  Bug:  we don't stat the file to find the length
*/
	if ( (strlen(localname) > strlen(MULTI_SUFFIX))
	   && (0==strcmp(localname + strlen(localname) - strlen(MULTI_SUFFIX),
	                  MULTI_SUFFIX))) {
	    DIR *dp;
	    struct direct * dirbuf;

	    float best = NO_VALUE_FOUND;	/* So far best is bad */
	    HTFormat best_rep = NULL;	/* Set when rep found */
	    struct direct best_dirbuf;	/* Best dir entry so far */

	    char * base = strrchr(localname, '/');
	    int baselen;

	    if (!base || base == localname) goto forget_multi;
	    *base++ = 0;		/* Just got directory name */
	    baselen = strlen(base)- strlen(MULTI_SUFFIX);
	    base[baselen] = 0;	/* Chop off suffix */

	    dp = opendir(localname);
	    if (!dp) {
forget_multi:
		free(filename);  
		free(localname);
		return HTLoadError(sink, 500,
			"Multiformat: directory scan failed.");
	    }
	    
	    while (dirbuf = readdir(dp)) {
			/* while there are directory entries to be read */
		if (dirbuf->d_ino == 0) continue;
				/* if the entry is not being used, skip it */
		
		if (dirbuf->d_namlen > baselen &&      /* Match? */
		    !strncmp(dirbuf->d_name, base, baselen)) {	
		    HTFormat rep = HTFileFormat(dirbuf->d_name);
		    float value = HTStackValue(rep, format_out,
		    				HTFileValue(dirbuf->d_name),
						0.0  /* @@@@@@ */);
		    if (value != NO_VALUE_FOUND) {
		        if (TRACE) fprintf(stderr,
				"HTFile: value of presenting %s is %f\n",
				HTAtom_name(rep), value);
			if  (value > best) {
			    best_rep = rep;
			    best = value;
			    best_dirbuf = *dirbuf;
		       }
		    }	/* if best so far */ 		    
		 } /* if match */  
		    
	    } /* end while directory entries left to read */
	    closedir(dp);
	    
	    if (best_rep) {
		format = best_rep;
		base[-1] = '/';		/* Restore directory name */
		base[0] = 0;
		StrAllocCat(localname, best_dirbuf.d_name);
		goto open_file;
		
	    } else { 			/* If not found suitable file */
		free(filename);  
		free(localname);
		return HTLoadError(sink, 403,	/* List formats? */
		   "Could not find suitable representation for transmission.");
	    }
	    /*NOTREACHED*/
	} /* if multi suffix */
/*
**	Check to see if the 'localname' is in fact a directory.  If it is
**	create a new hypertext object containing a list of files and 
**	subdirectories contained in the directory.  All of these are links
**      to the directories or files listed.
**      NB This assumes the existance of a type 'struct direct', which will
**      hold the directory entry, and a type 'DIR' which is used to point to
**      the current directory being read.
*/
	
	
	if (stat(localname,&dir_info) == -1) {     /* get file information */
	                               /* if can't read file information */
	    if (TRACE) fprintf(stderr, "HTFile: can't stat %s\n", localname);

	}  else {		/* Stat was OK */
		

	    if (((dir_info.st_mode) & S_IFMT) == S_IFDIR) {
		/* if localname is a directory */	

		HTStructured* target;		/* HTML object */
		HTStructuredClass targetClass;

		DIR *dp;
		struct direct * dirbuf;
		
		BOOL present[HTML_A_ATTRIBUTES];
		char * value[HTML_A_ATTRIBUTES];
		
		char * tmpfilename = NULL;
		char * shortfilename = NULL;
		struct stat file_info;
		
		if (TRACE)
		    fprintf(stderr,"%s is a directory\n",localname);
			
/*	Check directory access.
**	Selective access means only those directories containing a
**	marker file can be browsed
*/
		if (HTDirAccess == HT_DIR_FORBID) {
		    return HTLoadError(sink, 403,
		    "Directory browsing is not allowed.");
		}


		if (HTDirAccess == HT_DIR_SELECTIVE) {
		    char * enable_file_name = 
			malloc(strlen(localname)+ 1 +
			 strlen(HT_DIR_ENABLE_FILE) + 1);
		    strcpy(enable_file_name, localname);
		    strcat(enable_file_name, "/");
		    strcat(enable_file_name, HT_DIR_ENABLE_FILE);
		    if (stat(enable_file_name, &file_info) != 0) {
			free(filename);  
			free(localname);
			return HTLoadError(sink, 403,
			"Selective access is not enabled for this directory");
		    }
		}

 
		dp = opendir(localname);
		if (!dp) {
		    free(filename);  
		    free(localname);
		    return HTLoadError(sink, 403, "This directory is not readable.");
		}


 /*	Directory access is allowed and possible
 */
		target = HTML_new(anchor, sink);
		targetClass = *target->isa;	/* Copy routine entry points */
		    
  		{ int i;
			for(i=0; i<HTML_A_ATTRIBUTES; i++)
				present[i] = (i==HTML_A_HREF);
		}
		
		START(HTML_TITLE);
		PUTS(filename);
		END(HTML_TITLE);    

		START(HTML_H1);
		shortfilename=strrchr(localname,'/');
		/* put the last part of the path in shortfilename */
		shortfilename++;               /* get rid of leading '/' */
		if (*shortfilename=='\0')
		    shortfilename--;
		PUTS(shortfilename);
		END(HTML_H1);

                if (HTDirReadme == HT_DIR_README_TOP)
		    do_readme(target, localname);
		

#ifdef SORT
		HTBTree * bt = HTBTree_new(strcasecomp);
#endif
		START(HTML_DIR);		

		while (dirbuf = readdir(dp)) {
		    START(HTML_LI);
			    /* while there are directory entries to be read */
		    if (dirbuf->d_ino == 0)
				    /* if the entry is not being used, skip it */
			continue;
		    
		    if (!strcmp(dirbuf->d_name,"."))
			continue;      /* skip the entry for this directory */
			
		    if (strcmp(dirbuf->d_name,".."))
				/* if the current entry is parent directory */
			if ((*(dirbuf->d_name)=='.') ||
			    (*(dirbuf->d_name)==','))
			    continue;    /* skip those files whose name begins
					    with '.' or ',' */

		    StrAllocCopy(tmpfilename,localname);
		    if (strcmp(localname,"/")) 
					/* if filename is not root directory */
			StrAllocCat(tmpfilename,"/"); 
		    else
			if (!strcmp(dirbuf->d_name,".."))
			    continue;
	    /* if root directory and current entry is parent
				    directory, skip the current entry */
		    
		    StrAllocCat(tmpfilename,dirbuf->d_name);
		    /* append the current entry's filename to the path */
		    HTSimplify(tmpfilename);
	    
		    {
			char * relative = (char*) malloc(
			    strlen(shortfilename) + strlen(dirbuf->d_name)+2);
			if (relative == NULL) outofmem(__FILE__, "DirRead");
/*		sprintf(relative, "%s/%s", shortfilename, dirbuf->d_name);*/
/* Won't work for root, or mapped names, so use following line */
			sprintf(relative, "./%s", dirbuf->d_name);
			value[HTML_A_HREF] = relative;
			(*targetClass.start_element)(
				    target,HTML_A, present, value);
			free(relative);
		    }
		    stat(tmpfilename, &file_info);
		    
		    if (strcmp(dirbuf->d_name,"..")) {
/* if the current entry is not the parent directory then use the file name */
			PUTS(dirbuf->d_name);
			if (((file_info.st_mode) & S_IFMT) == S_IFDIR) 
			    PUTC('/'); 
		    }		        
		    else {
		    /* use name of parent directory */
			char * endbit = strrchr(tmpfilename, '/');
			PUTS("Up to ");
			PUTS(endbit?endbit+1:tmpfilename);
		    }	
		    END(HTML_A);
			
		} /* end while directory entries left to read */
		
		closedir(dp);
		free(tmpfilename);
		
#ifdef SORT
		{		/* Read out sorted filenames */
		    void * ele = NULL;
		    while ( (ele=HTBTree_next(bt, ele) != 0) {
		       if (TRACE) fprintf(stderr,
		       	"Sorted: %s\n", HTBTree_object(ele);
			/* @@@@@@@@@@@@@@@@@ */
		    }
		    HTBTree_free(bt);
		}
#endif		
		END(HTML_DIR);

		if (HTDirReadme == HT_DIR_README_BOTTOM)
		    do_readme(target, localname);

		END_TARGET;
		FREE_TARGET;
		    
		free(filename);  
		free(localname);
		return HT_LOADED;	/* document loaded */
		
	    } /* end if localname is directory */
	
	} /* end if file stat worked */
	
/* End of directory reading section
*/
#endif
open_file:
	fd = open(localname, O_RDONLY, 0);
	if(TRACE) printf ("HTAccess: Opening `%s' gives %d\n",
			    localname, fd);
	if (fd>=0) {		/* Good! */
	    if (HTEditable(localname)) {
	        HTAtom * put = HTAtom_for("PUT");
		HTList * methods = HTAnchor_methods(anchor);
		if (HTList_indexOf(methods, put) == (-1)) {
		    HTList_addObject(methods, put);
		}
	    }
	free(localname);
	}
    }  /* local unix file system */
#endif
#endif

#ifndef DECNET
/*	Now, as transparently mounted access has failed, we try FTP.
*/
    if (fd<0) {
	if (strcmp(nodename, HTHostName())!=0) {
	    free(filename);
	    fd = HTFTPLoad(addr, anchor, format_out, sink);
	    if (fd<0) return HTInetStatus("FTP file load");
	    return fd;
        }
    }
#endif

/*	All attempts have failed if fd<0.
*/
    if (fd<0) {
    	if (TRACE)
    	printf("Can't open `%s', errno=%d\n", filename, errno);
	free(filename);
	return HTLoadError(sink, 403, "Can't access requested file.");
    }
    
    HTParseSocket(format, format_out, anchor, fd, sink);
    NETCLOSE(fd);
    free(filename);
    return HT_LOADED;

}

/*		Protocol descriptors
*/
PUBLIC HTProtocol HTFTP  = { "ftp", HTLoadFile, 0 };
PUBLIC HTProtocol HTFile = { "file", HTLoadFile, HTFileSaveStream };

