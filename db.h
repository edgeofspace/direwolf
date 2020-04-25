
/* db.h */


#include "hdlc_rec2.h"		// for retry_t
#include "decode_aprs.h"	// for decode_aprs_t
#include "ax25_pad.h"
#include "log.h"
#include "config.h"

// This is for postgresql
#ifdef __APPLE__
#include </opt/local/include/postgresql96/libpq-fe.h>
#else
#include <postgresql/libpq-fe.h>
#endif


/* 
*
* These are the database functions 
*
*/

// db_init opens a DB connection to postgresql 
void db_init (struct misc_config_s *config);

//  db_write_recv will add a record to the aprs_packets table
void db_write_recv (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel);

//  db_write_xmit will add a record to the aprs_beacons table
//void db_write_xmit (int chan, decode_aprs_t *A, packet_t pp);

// db_term closes all database connections
void db_term (void);

// make_safe_text creates a string of the raw packet text that is safe for printing (or...for adding to a DB field)
char * make_ascii_only (char *pstr, int len, char *safe_str, int dest_size);

// to trim off trailing \r and \n characters
void trim (char *stuff);



#ifndef __cplusplus
#ifndef bool
#define bool int
#endif   /* ndef bool */

#ifndef true
#define true    ((bool) 1)
#endif   /* ndef true */
#ifndef false
#define false   ((bool) 0)
#endif   /* ndef false */
#endif   /* not C++ */

#ifndef TRUE
#define TRUE    1
#endif   /* TRUE */

#ifndef FALSE
#define FALSE   0
#endif   /* FALSE */


// To check if the dw_packets table exists, if not, then try and create it
bool check_dw_packets_table (void);
