/* SYSIFCOPT(*IFSIO) TERASPACE(*YES *TSIFC) STGMDL(*SNGLVL) */
/* ------------------------------------------------------------- *
 * Company . . . : System & Method A/S                           *
 * Design  . . . : Niels Liisberg                                *
 * Function  . . : NOX - JSON serializer                         *
 *                                                               *
 * By     Date     Task    Description                           *
 * NL     02.06.03 0000000 New program                           *
 * ------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <iconv.h>


#include <sys/stat.h>
#include "ostypes.h"
#include "sndpgmmsg.h"
#include "trycatch.h"
#include "rtvsysval.h"
#include "parms.h"
#include "utl100.h"
#include "mem001.h"
#include "varchar.h"
#include "streamer.h"
#include "jsonxml.h"


extern int   OutputCcsid;


/* ---------------------------------------------------------------------------
	--------------------------------------------------------------------------- */
static void   jx_EncodeJsonStream (PSTREAM p , PUCHAR in)
{
	PJWRITE pJw = p->handle;

	while (*in) {
		UCHAR c =  *in;
		if (c == '\n' ||  c == '\r' || c == '\t' || c == '\"' ) {
			stream_putc(p,pJw->backSlash);
			switch (c) {
				case '\n': c = 'n' ; break ;
				case '\r': c = 'r' ; break ;
				case '\t': c = 't' ; break ;
				case '\"': c = '"' ; break ;
			}
		}
		else if  (c  == pJw->backSlash) {
			if (in[1] != 'u') {  // Dont double escape unicode escape sequence
				stream_putc(p,pJw->backSlash);
			}
		}
		else if  (c  < ' ') {
			c = ' ';
		}
		stream_putc(p,c);
		in++;
	}
}
/* In the stream - only  ... 
	static PUCHAR jx_EncodeJson (PUCHAR out , PUCHAR in)
	{
		PUCHAR ret = out;
		while (*in) {
			UCHAR c =  *in;
			if (c == '\n' ||  c == '\r' || c == '\t' || c == '\"' ) {
				*(out++) = p->(PJWRITE)handle->backSlash;
			}
			else if  (c  == p->(PJWRITE)handle->backSlash) {
				if (in[1] != 'u') {  // Dont double escape unicode escape sequence
					*(out++) = p->(PJWRITE)handle->backSlash;
				}
			}
			else if  (c  < ' ') {
				c = ' ';
			}
			*(out++) = c;
			in++;
		}
		*(out++) = '\0';
		return ret;
	}
....
*/
static void indent (PSTREAM pStream , int indent)
{
	int i;
	PJWRITE pjWrite = pStream->handle;
	if (pjWrite->doTrim) return;

	//if(!pjWrite->wasHere) {
	//   pjWrite->wasHere = true;
	//} else {
		stream_putc(pStream, '\n');
	//}
	for(i=0;i<indent; i++) {
		stream_putc(pStream, '\t');
	}
}
/* --------------------------------------------------------------------------- */
void checkParentRelation(PJXNODE pNode , PJXNODE pParent)
{
	 if (pNode->pNodeParent != pParent) {
		try {
			joblog("Invalid parent relation %s , %s for %s",
				pNode->pNodeParent->Name,
				pParent->Name,
				pNode->Name
			);
		}
		catch (NULL) ;
	 }
}
/* --------------------------------------------------------------------------- */
static void  jsonStreamPrintObject  (PJXNODE pParent, PSTREAM pStream, SHORT level)
{
	PJXNODE pNode;
	PJWRITE pJw = pStream->handle;
	SHORT nextLevel = level +1;

	// indent (pStream ,level);
	stream_putc (pStream, pJw->curBeg);
	for (pNode = pParent->pNodeChildHead ; pNode ; pNode=pNode->pNodeSibling) {
		indent (pStream ,nextLevel);
		stream_printf (pStream, "%c%s%c:",pJw->quote, pNode->Name, pJw->quote);
		checkParentRelation(pNode , pParent);
		jsonStreamPrintNode (pNode , pStream, nextLevel);
		if (pNode->pNodeSibling) stream_putc  (pStream, ',' );
	}
	indent (pStream , level);
	stream_putc (pStream, pJw->curEnd);
}
/* --------------------------------------------------------------------------- */
static void  jsonStreamPrintArray (PJXNODE pParent, PSTREAM pStream, SHORT level)
{
	PJXNODE pNode;
	PJWRITE pJw = pStream->handle;
	SHORT nextLevel = level +1;

	// indent (pStream ,level);
	stream_putc (pStream, pJw->braBeg); 

	indent (pStream ,nextLevel);
	for (pNode = pParent->pNodeChildHead ; pNode ; pNode=pNode->pNodeSibling) {
		// indent (pStream ,nextLevel);
		checkParentRelation(pNode , pParent);
		jsonStreamPrintNode (pNode , pStream, nextLevel);
		if (pNode->pNodeSibling) stream_putc  (pStream, ',' );
	}
	indent (pStream , level);
	stream_putc (pStream, pJw->braEnd);
}
/* --------------------------------------------------------------------------- */
static void jsonStreamPrintValue   (PJXNODE pNode, PSTREAM pStream)
{
	PJWRITE pJw = pStream->handle;
	// Has value?
	if (pNode->Value && pNode->Value[0] > '\0') {
		if (pNode->isLiteral) {
			stream_puts (pStream, pNode->Value);
		} else {
			stream_putc(pStream , pJw->quote);
			jx_EncodeJsonStream(pStream ,pNode->Value);
			stream_putc(pStream , pJw->quote);
		}
	// Else it is some kind of null: Strings are "". Literals will return "null"
	} else {
		if (pNode->isLiteral) {
			stream_puts (pStream, "null");
		} else {
			stream_printf (pStream, "%c%c", pJw->quote, pJw->quote);
		}
	}
}
/* --------------------------------------------------------------------------- */
/* Invalid node types a just jeft out                                          */
/* --------------------------------------------------------------------------- */
static void  jsonStreamPrintNode (PJXNODE pNode, PSTREAM pStream, SHORT level)
{
	switch (pNode->type) {
		case OBJECT:
			jsonStreamPrintObject  (pNode, pStream, level);
			break;

		case ARRAY:
			jsonStreamPrintArray   (pNode, pStream, level);
			break;

		case VALUE:
		case POINTER_VALUE:
			jsonStreamPrintValue   (pNode, pStream);
			break;
	 }
}
/* --------------------------------------------------------------------------- */
void  jx_AsJsonStream (PJXNODE pNode, PSTREAM pStream)
{
	if (pNode == NULL) {
		stream_puts (pStream, "null");
	} else {
		jsonStreamPrintNode (pNode, pStream, 0);
	}
}
// ----------------------------------------------------------------------------
static LONG jx_memWriter  (PSTREAM p , PUCHAR buf , ULONG len)
{
	PJWRITE pjWrite = p->handle;
	ULONG newLen =  pjWrite->bufLen + len;
	if ( newLen  > pjWrite->maxSize) {
		ULONG restlen = pjWrite->maxSize - pjWrite->bufLen;
		memcpy ( pjWrite->buf +  pjWrite->bufLen , buf , restlen  );
		pjWrite->bufLen = pjWrite->maxSize;
		return pjWrite->bufLen;
	}
	memcpy ( pjWrite->buf +  pjWrite->bufLen , buf , len);
	pjWrite->bufLen += len;
	return pjWrite->bufLen;
}

// ----------------------------------------------------------------------------
static LONG jx_fileWriter  (PSTREAM p , PUCHAR buf , ULONG len)
{
	PJWRITE pjWrite = p->handle;
	int outlen = 4 * len;
	size_t inbytesleft, outbytesleft, rc;
	PUCHAR temp , input , output ;

	input = buf;
	inbytesleft  = len;

	output = temp = malloc   (outlen);
	outbytesleft = outlen;

	rc = iconv ( pjWrite->iconv , &input , &inbytesleft, &output , &outbytesleft);
	outlen = output - temp;
	fwrite (temp  , 1 , outlen , pjWrite->outFile);
	free (temp);
	return rc;
}

/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
LONG jx_AsJsonTextMem (PJXNODE pNode, PUCHAR buf , ULONG maxLenP)
{
	PNPMPARMLISTADDRP pParms = _NPMPARMLISTADDR();
	PSTREAM  pStream;
	LONG     len;
	PJWRITE  pjWrite;
	
	
	if (pNode == NULL) return 0;
	if (pNode->signature != NODESIG) {
		strcpy (buf, (PUCHAR) pNode);
		return strlen(buf);
	}

	pStream = stream_new (4096);
	pStream->writer  = jx_memWriter;
	pStream->handle = pjWrite = jx_newWriter();
	pjWrite->buf = buf;
	pjWrite->doTrim = true;
	pjWrite->maxSize =   pParms->OpDescList == NULL
					|| (pParms->OpDescList && pParms->OpDescList->NbrOfParms >= 3) ? maxLenP : MEMMAX;

	jx_AsJsonStream (pNode , pStream);
	len = pStream->totalSize;
	stream_putc   (pStream,'\0');
	stream_delete (pStream);
	jx_deleteWriter(pjWrite);
	return  len;

}
/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
static void  jx_AsJsonStreamRunner   (PSTREAM pStream)
{
	PJXNODE  pNode = pStream->context;
	jx_AsJsonStream (pNode , pStream);
}
/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
PSTREAM jx_Stream  (PJXNODE pNode)
{
	 PSTREAM  pStream;
	 LONG     len;
	 PJWRITE  pjWrite;
	 
	 pStream = stream_new (4096);
	 pStream->handle = pjWrite = jx_newWriter();
	 pjWrite->doTrim  = true;
	 pjWrite->maxSize = MEMMAX;
	 pStream->runner  = jx_AsJsonStreamRunner;
	 pStream->context = pNode;
	 return  pStream;
}

/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
VARCHAR jx_AsJsonText (PJXNODE pNode)
{
	 VARCHAR  res;
	 res.Length = jx_AsJsonTextMem ( pNode ,  res.String, sizeof(res.String));
	 return res;
}
/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */

PJWRITE jx_newWriter ()
{
	PJWRITE pjWrite = malloc (sizeof(JWRITE)); 
	memset(pjWrite , 0 , sizeof(JWRITE) - sizeof(pjWrite->filler));
	#pragma convert(1252)
	XlateBufferQ(&pjWrite->braBeg , "[]{}\\\"" , 6, 1252 ,0 ); ;
	#pragma convert(0)
	return pjWrite; 
}
/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
void jx_deleteWriter (PJWRITE  pjWrite)
{
	free(pjWrite);
}
/* ---------------------------------------------------------------------------
	 --------------------------------------------------------------------------- */
void jx_WriteJsonStmf (PJXNODE pNode, PUCHAR FileName, int Ccsid, LGL trimOut, PJXNODE options)
{
	PNPMPARMLISTADDRP pParms = _NPMPARMLISTADDR();
	PSTREAM pStream;
	PJWRITE pjWrite;
	UCHAR   mode[32];
	UCHAR  sigUtf8[]  =  {0xef , 0xbb , 0xbf , 0x00};
	UCHAR  sigUtf16[] =  {0xff , 0xfe , 0x00};

	// Hack for quick fix no bom , just set ccsid negative
	BOOL   makeBomCode  = Ccsid > 0;
	Ccsid = Ccsid < 0  ? - Ccsid : Ccsid;



	if (pNode == NULL) return;

	pjWrite = jx_newWriter();

	pStream = stream_new (4096);
	pStream->writer = jx_fileWriter;

	sprintf(mode , "wb,o_ccsid=%d", Ccsid);
	unlink  ( strTrim(FileName)); // Just to reset the CCSID which will not change if file exists
	pjWrite->outFile  = fopen ( strTrim(FileName) , mode );
	if (pjWrite->outFile == NULL) {
		jx_deleteWriter(pjWrite);
		return;
	}

	pStream->handle = pjWrite;

	pjWrite->doTrim = (pParms->OpDescList && pParms->OpDescList->NbrOfParms >= 4 && trimOut == OFF) ? FALSE : TRUE;
	pjWrite->iconv  = XlateOpenDescriptor(OutputCcsid , Ccsid , false);

	if (makeBomCode) {
		switch(Ccsid) {
			case 1208 :
				fputs (sigUtf8, pjWrite->outFile );
				break;
			case 1200 :
				fputs (sigUtf16, pjWrite->outFile );
				break;
		}
	}

	// Any ascii will use basic ascii chars for building the document
	if (Ccsid = 1208 || Ccsid== 1252) {
		#pragma convert(1252)
		XlateBufferQ(&pjWrite->braBeg , "[]{}\\\"" , 6, 1252 ,0 ); ;
		#pragma convert(0)
	}

	jx_AsJsonStream (pNode , pStream);

	stream_delete (pStream);
	fclose(pjWrite->outFile);
	iconv_close(pjWrite->iconv);
	jx_deleteWriter(pjWrite);
}
